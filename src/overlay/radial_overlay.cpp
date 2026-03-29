#include "overlay/radial_overlay.h"
#include <spdlog/spdlog.h>
#include <cmath>
#include <chrono>
#include <thread>

namespace windeck::overlay {

static constexpr float kPi     = 3.14159265f;
static constexpr float kCx     = RadialOverlay::kWidth  / 2.0f;
static constexpr float kCy     = RadialOverlay::kHeight / 2.0f;
static constexpr float kOuterR = 98.0f;
static constexpr float kInnerR = 28.0f;
static constexpr float kLabelR = 66.0f;

static D2D1_POINT_2F polar(float cx, float cy, float r, float deg) {
    float rad = deg * kPi / 180.0f;
    return {cx + r * cosf(rad), cy + r * sinf(rad)};
}

RadialOverlay::RadialOverlay() {
    thread_ = std::thread(&RadialOverlay::thread_main, this);
    while (!initialized_.load(std::memory_order_acquire))
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

RadialOverlay::~RadialOverlay() {
    if (hwnd_) PostMessage(hwnd_, WM_CLOSE, 0, 0);
    if (thread_.joinable()) thread_.join();
}

void RadialOverlay::set_labels(const std::array<std::string, 8>& labels) {
    std::lock_guard lock(labels_mtx_);
    for (int i = 0; i < 8; i++) {
        int n = MultiByteToWideChar(CP_UTF8, 0, labels[i].c_str(), -1, nullptr, 0);
        if (n > 0) {
            labels_[i].resize(n - 1);
            MultiByteToWideChar(CP_UTF8, 0, labels[i].c_str(), -1, labels_[i].data(), n);
        }
    }
}

void RadialOverlay::show(uint8_t side) {
    side_.store(side, std::memory_order_relaxed);
    if (hwnd_) PostMessage(hwnd_, WM_OVR_SHOW, side, 0);
}

void RadialOverlay::update_sector(int sector) {
    active_sector_.store(sector, std::memory_order_relaxed);
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void RadialOverlay::hide() {
    if (hwnd_) PostMessage(hwnd_, WM_OVR_HIDE, 0, 0);
}

void RadialOverlay::thread_main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory_))) {
        spdlog::warn("RadialOverlay: D2D1CreateFactory failed");
        initialized_.store(true);
        CoUninitialize();
        return;
    }
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(&dwrite_)))) {
        spdlog::warn("RadialOverlay: DWriteCreateFactory failed");
        factory_->Release(); factory_ = nullptr;
        initialized_.store(true);
        CoUninitialize();
        return;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = s_wnd_proc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"WinDeckRadialOverlay";
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        L"WinDeckRadialOverlay", L"",
        WS_POPUP,
        0, 0, kWidth, kHeight,
        nullptr, nullptr, GetModuleHandleW(nullptr), this);

    if (!hwnd_) {
        spdlog::warn("RadialOverlay: CreateWindowExW failed ({})", GetLastError());
        initialized_.store(true);
        if (dwrite_) { dwrite_->Release(); dwrite_ = nullptr; }
        if (factory_) { factory_->Release(); factory_ = nullptr; }
        CoUninitialize();
        return;
    }

    SetLayeredWindowAttributes(hwnd_, kColorKey, kGlobalAlpha, LWA_COLORKEY | LWA_ALPHA);

    if (FAILED(create_resources())) {
        spdlog::warn("RadialOverlay: D2D resource creation failed");
    }

    initialized_.store(true, std::memory_order_release);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    release_resources();
    if (dwrite_)  { dwrite_->Release();  dwrite_  = nullptr; }
    if (factory_) { factory_->Release(); factory_ = nullptr; }
    CoUninitialize();
}

LRESULT CALLBACK RadialOverlay::s_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    RadialOverlay* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<RadialOverlay*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<RadialOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->wnd_proc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT RadialOverlay::wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT:
            on_paint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_OVR_SHOW:
            on_show(static_cast<uint8_t>(wp));
            return 0;
        case WM_OVR_HIDE:
            on_hide();
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void RadialOverlay::on_show(uint8_t side) {
    reposition(side);
    ShowWindow(hwnd_, SW_SHOWNA);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void RadialOverlay::on_hide() {
    ShowWindow(hwnd_, SW_HIDE);
}

void RadialOverlay::reposition(uint8_t side) {
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    constexpr int margin = 25;
    int x = (side == 1) ? margin : (sw - kWidth - margin);
    int y = sh - kHeight - 50;
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, kWidth, kHeight,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void RadialOverlay::on_paint() {
    if (!rt_) {
        if (FAILED(create_resources())) {
            ValidateRect(hwnd_, nullptr);
            return;
        }
    }

    rt_->BeginDraw();
    rt_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f)); // black = color key = transparent

    int active = active_sector_.load(std::memory_order_relaxed);

    for (int i = 0; i < 8; i++) draw_wedge(i, i == active);

    // Center dead-zone circle
    rt_->FillEllipse(
        D2D1::Ellipse(D2D1::Point2F(kCx, kCy), kInnerR - 1.0f, kInnerR - 1.0f),
        br_center_);

    for (int i = 0; i < 8; i++) draw_label(i);

    HRESULT hr = rt_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) release_resources();
    ValidateRect(hwnd_, nullptr);
}

void RadialOverlay::draw_wedge(int sector, bool active) {
    if (!factory_ || !rt_) return;

    // D2D angle: 0 degrees=east, CW+. North = -90 degrees.
    // Sector i center = -90 + sector*45 degrees.
    // Sector i spans +-22.5 degrees around center, with 2 degree gap each side -> span = 41 degrees.
    float gap   = 2.0f;
    float start = -112.5f + sector * 45.0f + gap;
    float end   = start + 45.0f - 2.0f * gap;

    D2D1_POINT_2F p0 = polar(kCx, kCy, kInnerR, start);
    D2D1_POINT_2F p1 = polar(kCx, kCy, kOuterR, start);
    D2D1_POINT_2F p2 = polar(kCx, kCy, kOuterR, end);
    D2D1_POINT_2F p3 = polar(kCx, kCy, kInnerR, end);

    ID2D1PathGeometry* geom = nullptr;
    if (FAILED(factory_->CreatePathGeometry(&geom))) return;

    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(geom->Open(&sink))) { geom->Release(); return; }

    sink->SetFillMode(D2D1_FILL_MODE_WINDING);
    sink->BeginFigure(p0, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(p1);

    D2D1_ARC_SEGMENT arc_out{};
    arc_out.point          = p2;
    arc_out.size           = {kOuterR, kOuterR};
    arc_out.rotationAngle  = 0.0f;
    arc_out.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
    arc_out.arcSize        = D2D1_ARC_SIZE_SMALL;
    sink->AddArc(arc_out);

    sink->AddLine(p3);

    D2D1_ARC_SEGMENT arc_in{};
    arc_in.point          = p0;
    arc_in.size           = {kInnerR, kInnerR};
    arc_in.rotationAngle  = 0.0f;
    arc_in.sweepDirection = D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
    arc_in.arcSize        = D2D1_ARC_SIZE_SMALL;
    sink->AddArc(arc_in);

    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    sink->Release();

    rt_->FillGeometry(geom, active ? br_active_ : br_sector_);
    geom->Release();
}

void RadialOverlay::draw_label(int sector) {
    if (!rt_ || !text_fmt_) return;

    float angle_deg = -90.0f + sector * 45.0f;
    D2D1_POINT_2F pos = polar(kCx, kCy, kLabelR, angle_deg);

    std::wstring text;
    {
        std::lock_guard lock(labels_mtx_);
        text = labels_[sector];
    }
    if (text.empty()) return;

    constexpr float bw = 44.0f, bh = 22.0f;
    D2D1_RECT_F rect = D2D1::RectF(
        pos.x - bw / 2.0f, pos.y - bh / 2.0f,
        pos.x + bw / 2.0f, pos.y + bh / 2.0f);

    rt_->DrawText(text.c_str(), static_cast<UINT32>(text.size()),
                  text_fmt_, rect, br_text_,
                  D2D1_DRAW_TEXT_OPTIONS_NONE,
                  DWRITE_MEASURING_MODE_NATURAL);
}

HRESULT RadialOverlay::create_resources() {
    if (!factory_ || !dwrite_ || !hwnd_) return E_FAIL;
    release_resources();

    D2D1_RENDER_TARGET_PROPERTIES rt_props =
        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwnd_props =
        D2D1::HwndRenderTargetProperties(hwnd_,
            D2D1::SizeU(kWidth, kHeight));

    HRESULT hr = factory_->CreateHwndRenderTarget(rt_props, hwnd_props, &rt_);
    if (FAILED(hr)) return hr;

    auto mk = [&](float r, float g, float b, ID2D1SolidColorBrush** out) -> HRESULT {
        return rt_->CreateSolidColorBrush(D2D1::ColorF(r, g, b), out);
    };
    if (FAILED(mk(0.149f, 0.149f, 0.149f, &br_sector_))) return E_FAIL; // #262626
    if (FAILED(mk(0.902f, 0.600f, 0.098f, &br_active_))) return E_FAIL; // orange
    if (FAILED(mk(0.078f, 0.078f, 0.078f, &br_center_))) return E_FAIL; // near-black
    if (FAILED(mk(1.000f, 1.000f, 1.000f, &br_text_)))   return E_FAIL; // white

    hr = dwrite_->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        14.0f, L"en-US", &text_fmt_);
    if (FAILED(hr)) return hr;

    text_fmt_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    text_fmt_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    return S_OK;
}

void RadialOverlay::release_resources() {
    auto rel = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };
    rel(text_fmt_);
    rel(br_text_);
    rel(br_center_);
    rel(br_active_);
    rel(br_sector_);
    rel(rt_);
}

} // namespace windeck::overlay
