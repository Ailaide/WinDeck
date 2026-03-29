#include "overlay/radial_overlay.h"
#include <spdlog/spdlog.h>
#include <cmath>

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

RadialOverlay::RadialOverlay()
    : OverlayBase(L"WinDeckRadialOverlay", kWidth, kHeight) {
    start();
}

void RadialOverlay::set_labels(const std::array<std::string, 8>& labels) {
    std::lock_guard lock(labels_mtx_);
    for (int i = 0; i < 8; i++) {
        int len = static_cast<int>(labels[i].size());
        int n = MultiByteToWideChar(CP_UTF8, 0, labels[i].c_str(), len, nullptr, 0);
        labels_[i].resize(n);
        MultiByteToWideChar(CP_UTF8, 0, labels[i].c_str(), len, labels_[i].data(), n);
    }
}

void RadialOverlay::show(uint8_t side) {
    side_.store(side, std::memory_order_relaxed);
    post_show(side);
}

void RadialOverlay::update_sector(int hovered, bool pressed, uint8_t side) {
    if (side != side_.load(std::memory_order_relaxed)) return;
    int prev_hovered = active_sector_.exchange(hovered, std::memory_order_relaxed);
    bool prev_pressed = pressed_.exchange(pressed, std::memory_order_relaxed);
    if (prev_hovered != hovered || prev_pressed != pressed)
        invalidate();
}

void RadialOverlay::hide(uint8_t side) {
    if (side != side_.load(std::memory_order_relaxed)) return;
    post_hide();
}

void RadialOverlay::on_show(WPARAM wp) {
    reposition(static_cast<uint8_t>(wp));
    OverlayBase::on_show(wp);
}

void RadialOverlay::reposition(uint8_t side) {
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    constexpr int margin = 25;
    int x = (side == 1) ? margin : (sw - kWidth - margin);
    int y = sh - kHeight - 50;
    SetWindowPos(hwnd(), HWND_TOPMOST, x, y, kWidth, kHeight,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void RadialOverlay::on_paint() {
    if (!rt_) {
        if (FAILED(recreate_resources())) {
            ValidateRect(hwnd(), nullptr);
            return;
        }
    }

    rt_->BeginDraw();
    rt_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f)); // black = color key = transparent

    int hovered = active_sector_.load(std::memory_order_relaxed);
    bool pressed = pressed_.load(std::memory_order_relaxed);

    for (int i = 0; i < 8; i++) draw_wedge(i, hovered, pressed);

    // Center dead-zone circle
    rt_->FillEllipse(
        D2D1::Ellipse(D2D1::Point2F(kCx, kCy), kInnerR - 1.0f, kInnerR - 1.0f),
        br_center_);

    for (int i = 0; i < 8; i++) draw_label(i);

    HRESULT hr = rt_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        release_child_resources();
        rt_ = nullptr; // triggers full recreate on next paint
    }
    ValidateRect(hwnd(), nullptr);
}

HRESULT RadialOverlay::create_child_resources() {
    build_wedge_geometry();
    return S_OK;
}

void RadialOverlay::release_child_resources() {
    for (auto*& g : wedge_geom_) {
        if (g) { g->Release(); g = nullptr; }
    }
}

void RadialOverlay::build_wedge_geometry() {
    if (!factory_) return;
    constexpr float kHalfGap = 1.5f;
    float inner_offset = asinf(kHalfGap / kInnerR) * 180.0f / kPi;
    float outer_offset = asinf(kHalfGap / kOuterR) * 180.0f / kPi;

    for (int i = 0; i < 8; i++) {
        float boundary_start = -112.5f + i * 45.0f;
        float boundary_end   = boundary_start + 45.0f;

        D2D1_POINT_2F p0 = polar(kCx, kCy, kInnerR, boundary_start + inner_offset);
        D2D1_POINT_2F p1 = polar(kCx, kCy, kOuterR, boundary_start + outer_offset);
        D2D1_POINT_2F p2 = polar(kCx, kCy, kOuterR, boundary_end   - outer_offset);
        D2D1_POINT_2F p3 = polar(kCx, kCy, kInnerR, boundary_end   - inner_offset);

        ID2D1PathGeometry* geom = nullptr;
        if (FAILED(factory_->CreatePathGeometry(&geom))) continue;

        ID2D1GeometrySink* sink = nullptr;
        if (FAILED(geom->Open(&sink))) { geom->Release(); continue; }

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

        wedge_geom_[i] = geom;
    }
}

void RadialOverlay::draw_wedge(int sector, int hovered, bool pressed) {
    if (!rt_ || !wedge_geom_[sector]) return;

    ID2D1SolidColorBrush* brush = br_sector_;
    if (sector == hovered) brush = pressed ? br_active_ : br_hover_;
    rt_->FillGeometry(wedge_geom_[sector], brush);
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

} // namespace windeck::overlay
