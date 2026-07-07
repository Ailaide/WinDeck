#include "overlay/overlay_base.h"
#include <spdlog/spdlog.h>
#include <chrono>

namespace windeck::overlay {

OverlayBase::OverlayBase(const wchar_t* class_name, int width, int height)
    : class_name_(class_name), width_(width), height_(height) {}

void OverlayBase::start() {
    thread_ = std::thread(&OverlayBase::thread_main, this);
    while (!initialized_.load(std::memory_order_acquire))
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

OverlayBase::~OverlayBase() {
    if (hwnd_) PostMessage(hwnd_, WM_CLOSE, 0, 0);
    if (thread_.joinable()) thread_.join();
}

void OverlayBase::post_show(WPARAM wp) {
    if (hwnd_) PostMessage(hwnd_, WM_OVR_SHOW, wp, 0);
}

void OverlayBase::post_hide() {
    if (hwnd_) PostMessage(hwnd_, WM_OVR_HIDE, 0, 0);
}

void OverlayBase::invalidate() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void OverlayBase::on_show(WPARAM /*wp*/) {
    ShowWindow(hwnd_, SW_SHOWNA);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void OverlayBase::on_hide() {
    ShowWindow(hwnd_, SW_HIDE);
}

bool OverlayBase::on_app_message(UINT /*msg*/, WPARAM /*wp*/, LPARAM /*lp*/) {
    return false;
}

void OverlayBase::thread_main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory_))) {
        spdlog::warn("{}: D2D1CreateFactory failed", "OverlayBase");
        initialized_.store(true);
        CoUninitialize();
        return;
    }
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(&dwrite_)))) {
        spdlog::warn("{}: DWriteCreateFactory failed", "OverlayBase");
        factory_->Release(); factory_ = nullptr;
        initialized_.store(true);
        CoUninitialize();
        return;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = s_wnd_proc;
    wc.hInstance      = GetModuleHandleW(nullptr);
    wc.lpszClassName  = class_name_;
    wc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        class_name_, L"",
        WS_POPUP,
        0, 0, width_, height_,
        nullptr, nullptr, GetModuleHandleW(nullptr), this);

    if (!hwnd_) {
        spdlog::warn("OverlayBase: CreateWindowExW failed ({})", GetLastError());
        initialized_.store(true);
        if (dwrite_) { dwrite_->Release(); dwrite_ = nullptr; }
        if (factory_) { factory_->Release(); factory_ = nullptr; }
        CoUninitialize();
        return;
    }

    SetLayeredWindowAttributes(hwnd_, OverlayColors::kColorKey,
                               OverlayColors::kGlobalAlpha,
                               LWA_COLORKEY | LWA_ALPHA);

    if (FAILED(create_resources())) {
        spdlog::warn("OverlayBase: D2D resource creation failed");
    }

    initialized_.store(true, std::memory_order_release);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    release_resources();
    UnregisterClassW(class_name_, GetModuleHandleW(nullptr));
    if (dwrite_)  { dwrite_->Release();  dwrite_  = nullptr; }
    if (factory_) { factory_->Release(); factory_ = nullptr; }
    CoUninitialize();
}

LRESULT CALLBACK OverlayBase::s_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    OverlayBase* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<OverlayBase*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<OverlayBase*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->wnd_proc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT OverlayBase::wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT:
            on_paint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_OVR_SHOW:
            on_show(wp);
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
    // Let subclasses handle custom WM_APP messages
    if (msg >= WM_APP && on_app_message(msg, wp, lp))
        return 0;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HRESULT OverlayBase::create_resources() {
    if (!factory_ || !dwrite_ || !hwnd_) return E_FAIL;
    release_resources();

    D2D1_RENDER_TARGET_PROPERTIES rt_props =
        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwnd_props =
        D2D1::HwndRenderTargetProperties(hwnd_,
            D2D1::SizeU(width_, height_));

    HRESULT hr = factory_->CreateHwndRenderTarget(rt_props, hwnd_props, &rt_);
    if (FAILED(hr)) return hr;

    auto mk = [&](const D2D1_COLOR_F& c, ID2D1SolidColorBrush** out) -> HRESULT {
        return rt_->CreateSolidColorBrush(c, out);
    };
    if (FAILED(mk(OverlayColors::kSector, &br_sector_))) return E_FAIL;
    if (FAILED(mk(OverlayColors::kHover,  &br_hover_)))  return E_FAIL;
    if (FAILED(mk(OverlayColors::kActive, &br_active_))) return E_FAIL;
    if (FAILED(mk(OverlayColors::kCenter, &br_center_))) return E_FAIL;
    if (FAILED(mk(OverlayColors::kText,   &br_text_)))   return E_FAIL;

    hr = dwrite_->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        14.0f, L"en-US", &text_fmt_);
    if (FAILED(hr)) return hr;

    text_fmt_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    text_fmt_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    return create_child_resources();
}

HRESULT OverlayBase::recreate_resources() {
    release_resources();
    return create_resources();
}

void OverlayBase::release_resources() {
    release_child_resources();
    auto rel = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };
    rel(text_fmt_);
    rel(br_text_);
    rel(br_center_);
    rel(br_active_);
    rel(br_hover_);
    rel(br_sector_);
    rel(rt_);
}

} // namespace windeck::overlay
