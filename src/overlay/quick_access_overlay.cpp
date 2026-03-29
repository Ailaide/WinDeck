#include "overlay/quick_access_overlay.h"
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>

namespace windeck::overlay {

// --- Mode cycling helpers ---

static config::StickMode next_stick_mode(config::StickMode m) {
    switch (m) {
        case config::StickMode::Stick:  return config::StickMode::Wasd;
        case config::StickMode::Wasd:   return config::StickMode::Arrows;
        case config::StickMode::Arrows: return config::StickMode::Mouse;
        case config::StickMode::Mouse:  return config::StickMode::Stick;
    }
    return config::StickMode::Stick;
}

static config::TouchpadMode next_touchpad_mode(config::TouchpadMode m) {
    switch (m) {
        case config::TouchpadMode::TrackpadInertia: return config::TouchpadMode::Mouse;
        case config::TouchpadMode::Mouse:           return config::TouchpadMode::Wasd;
        case config::TouchpadMode::Wasd:            return config::TouchpadMode::Arrows;
        case config::TouchpadMode::Arrows:          return config::TouchpadMode::RadialMenu;
        case config::TouchpadMode::RadialMenu:      return config::TouchpadMode::DpadButtons;
        case config::TouchpadMode::DpadButtons:     return config::TouchpadMode::Scroll;
        case config::TouchpadMode::Scroll:          return config::TouchpadMode::Disabled;
        case config::TouchpadMode::Disabled:        return config::TouchpadMode::TrackpadInertia;
    }
    return config::TouchpadMode::TrackpadInertia;
}

static config::DpadMode next_dpad_mode(config::DpadMode m) {
    switch (m) {
        case config::DpadMode::Dpad:   return config::DpadMode::Wasd;
        case config::DpadMode::Wasd:   return config::DpadMode::Arrows;
        case config::DpadMode::Arrows: return config::DpadMode::Dpad;
    }
    return config::DpadMode::Dpad;
}

static config::GyroMode next_gyro_mode(config::GyroMode m) {
    switch (m) {
        case config::GyroMode::Disabled: return config::GyroMode::Mouse;
        case config::GyroMode::Mouse:    return config::GyroMode::Disabled;
    }
    return config::GyroMode::Disabled;
}

// --- Mode name helpers ---

static const wchar_t* stick_mode_name(config::StickMode m) {
    switch (m) {
        case config::StickMode::Stick:  return L"Stick";
        case config::StickMode::Wasd:   return L"WASD";
        case config::StickMode::Arrows: return L"Arrows";
        case config::StickMode::Mouse:  return L"Mouse";
    }
    return L"?";
}

static const wchar_t* touchpad_mode_name(config::TouchpadMode m) {
    switch (m) {
        case config::TouchpadMode::TrackpadInertia: return L"Trackball";
        case config::TouchpadMode::Mouse:           return L"Mouse";
        case config::TouchpadMode::Wasd:            return L"WASD";
        case config::TouchpadMode::Arrows:          return L"Arrows";
        case config::TouchpadMode::RadialMenu:      return L"Radial";
        case config::TouchpadMode::DpadButtons:     return L"D-Pad";
        case config::TouchpadMode::Scroll:          return L"Scroll";
        case config::TouchpadMode::Disabled:        return L"Disabled";
    }
    return L"?";
}

static const wchar_t* dpad_mode_name(config::DpadMode m) {
    switch (m) {
        case config::DpadMode::Dpad:   return L"D-Pad";
        case config::DpadMode::Wasd:   return L"WASD";
        case config::DpadMode::Arrows: return L"Arrows";
    }
    return L"?";
}

static const wchar_t* gyro_mode_name(config::GyroMode m) {
    switch (m) {
        case config::GyroMode::Disabled: return L"Disabled";
        case config::GyroMode::Mouse:    return L"Mouse";
    }
    return L"?";
}

static const wchar_t* accel_name(config::TouchpadAccelerationPreset a) {
    switch (a) {
        case config::TouchpadAccelerationPreset::Off:    return L"Off";
        case config::TouchpadAccelerationPreset::Normal: return L"Normal";
        case config::TouchpadAccelerationPreset::Strong: return L"Strong";
    }
    return L"?";
}

static config::TouchpadAccelerationPreset next_accel(config::TouchpadAccelerationPreset a) {
    switch (a) {
        case config::TouchpadAccelerationPreset::Off:    return config::TouchpadAccelerationPreset::Normal;
        case config::TouchpadAccelerationPreset::Normal: return config::TouchpadAccelerationPreset::Strong;
        case config::TouchpadAccelerationPreset::Strong: return config::TouchpadAccelerationPreset::Off;
    }
    return config::TouchpadAccelerationPreset::Normal;
}

static const wchar_t* curve_name(config::ResponseCurve c) {
    switch (c) {
        case config::ResponseCurve::Linear:          return L"Linear";
        case config::ResponseCurve::Quadratic:       return L"Quadratic";
        case config::ResponseCurve::InverseQuadratic: return L"Inv. Quadratic";
    }
    return L"?";
}

static config::ResponseCurve next_curve(config::ResponseCurve c) {
    switch (c) {
        case config::ResponseCurve::Linear:          return config::ResponseCurve::Quadratic;
        case config::ResponseCurve::Quadratic:       return config::ResponseCurve::InverseQuadratic;
        case config::ResponseCurve::InverseQuadratic: return config::ResponseCurve::Linear;
    }
    return config::ResponseCurve::Linear;
}

// --- Tile label constants ---
static const wchar_t* kTileLabels[] = {
    L"D-Pad", L"L-Stick", L"R-Stick",
    L"Gyro",  L"L-Pad",   L"R-Pad"
};

// --- Format float to wstring ---
static std::wstring fmt_float(float v, int decimals) {
    wchar_t buf[32];
    swprintf(buf, 32, decimals == 1 ? L"%.1f" : L"%.2f", v);
    return buf;
}

// --- QuickAccessOverlay implementation ---

QuickAccessOverlay::QuickAccessOverlay(windeck::config::Config& config,
                                       windeck::mapper::Mapper& mapper,
                                       windeck::output::SyntheticInput& synth)
    : OverlayBase(L"WinDeckQuickAccess", kTotalW, kTotalH)
    , config_(config), mapper_(mapper), synth_(synth) {
    for (int i = 0; i < kTiles; i++)
        tiles_[i].label = kTileLabels[i];

    // Initialize param definitions
    params_[0] = {L"Touchpads Sensitivity", L"1.0", ParamType::Slider, 1.0f, 0.1f, 5.0f, 0.1f};
    params_[1] = {L"Touchpads Inertia",    L"0.95", ParamType::Slider, 0.95f, 0.80f, 0.99f, 0.01f};
    params_[2] = {L"Touchpads Acceleration", L"Normal", ParamType::Button, 0, 0, 0, 0};
    params_[3] = {L"Sticks Curve",          L"Linear", ParamType::Button, 0, 0, 0, 0};
    params_[4] = {L"Diagonal Overlap",      L"0.00", ParamType::Slider, 0.0f, 0.0f, 1.0f, 0.05f};

    start();
}

void QuickAccessOverlay::show() {
    visible_.store(true, std::memory_order_relaxed);
    cursor_.store(0, std::memory_order_relaxed);
    post_show();
}

void QuickAccessOverlay::hide() {
    visible_.store(false, std::memory_order_relaxed);
    post_hide();
}

void QuickAccessOverlay::navigate(int dx, int dy) {
    int cur = cursor_.load(std::memory_order_relaxed);

    if (cur < kTiles) {
        // In tiles zone
        int col = cur % kCols;
        int row = cur / kCols;

        if (dy != 0) {
            int new_row = row + dy;
            if (new_row < 0) {
                // Up from row 0 -> last param
                cursor_.store(kTiles + kParams - 1, std::memory_order_relaxed);
                invalidate();
                return;
            } else if (new_row >= kRows) {
                // Down from last tile row -> first param
                cursor_.store(kTiles, std::memory_order_relaxed);
                invalidate();
                return;
            }
            int next = new_row * kCols + col;
            cursor_.store(next, std::memory_order_relaxed);
            invalidate();
        }
        if (dx != 0) {
            col = (col + dx + kCols) % kCols;
            int next = row * kCols + col;
            cursor_.store(next, std::memory_order_relaxed);
            invalidate();
        }
    } else {
        // In params zone
        int param_idx = cur - kTiles;

        if (dy != 0) {
            int new_idx = param_idx + dy;
            if (new_idx < 0) {
                // Up from first param -> tiles row 1, center column
                cursor_.store(1 * kCols + 1, std::memory_order_relaxed);
                invalidate();
                return;
            } else if (new_idx >= kParams) {
                // Down from last param -> tiles row 0, center column
                cursor_.store(0 * kCols + 1, std::memory_order_relaxed);
                invalidate();
                return;
            }
            cursor_.store(kTiles + new_idx, std::memory_order_relaxed);
            invalidate();
        }

        if (dx != 0) {
            // Adjust slider value
            std::lock_guard lock(tiles_mtx_);
            auto& p = params_[param_idx];
            if (p.type == ParamType::Slider) {
                p.value = std::clamp(p.value + dx * p.step, p.min_val, p.max_val);
                int dec = (p.step < 0.05f) ? 2 : 1;
                p.value_text = fmt_float(p.value, dec);

                // Apply to profile
                auto profile = config_.get_profile();
                switch (param_idx) {
                    case 0: // Sensitivity
                        profile.left_touchpad.sensitivity = p.value;
                        profile.right_touchpad.sensitivity = p.value;
                        break;
                    case 1: // Friction
                        profile.left_touchpad.inertia = p.value;
                        profile.right_touchpad.inertia = p.value;
                        break;
                    case 4: // Overlap
                        profile.overlap = p.value;
                        break;
                }
                config_.set_profile(profile);
                mapper_.apply_profile(profile, synth_);
                invalidate();
            }
        }
    }
}

void QuickAccessOverlay::activate() {
    int cur = cursor_.load(std::memory_order_relaxed);

    if (cur < kTiles) {
        // Tile: cycle mode
        auto profile = config_.get_profile();
        switch (static_cast<TileId>(cur)) {
            case DPad:   profile.dpad.mode = next_dpad_mode(profile.dpad.mode); break;
            case LStick: profile.left_stick.mode = next_stick_mode(profile.left_stick.mode); break;
            case RStick: profile.right_stick.mode = next_stick_mode(profile.right_stick.mode); break;
            case Gyro:   profile.gyro.mode = next_gyro_mode(profile.gyro.mode); break;
            case LPad:   profile.left_touchpad.mode = next_touchpad_mode(profile.left_touchpad.mode); break;
            case RPad:   profile.right_touchpad.mode = next_touchpad_mode(profile.right_touchpad.mode); break;
        }
        config_.set_profile(profile);
        mapper_.apply_profile(profile, synth_);
        update_tiles(profile);
    } else {
        // Param: cycle button-type params
        int param_idx = cur - kTiles;
        std::lock_guard lock(tiles_mtx_);
        auto& p = params_[param_idx];
        if (p.type != ParamType::Button) return;

        auto profile = config_.get_profile();
        switch (param_idx) {
            case 2: { // Acceleration
                auto next = next_accel(profile.left_touchpad.acceleration);
                profile.left_touchpad.acceleration = next;
                profile.right_touchpad.acceleration = next;
                p.value_text = accel_name(next);
                break;
            }
            case 3: { // Curve
                auto next = next_curve(profile.left_stick.curve);
                profile.left_stick.curve = next;
                profile.right_stick.curve = next;
                p.value_text = curve_name(next);
                break;
            }
        }
        config_.set_profile(profile);
        mapper_.apply_profile(profile, synth_);
        invalidate();
    }
}

void QuickAccessOverlay::update_tiles(const config::Profile& profile) {
    std::lock_guard lock(tiles_mtx_);
    tiles_[DPad].mode_text    = dpad_mode_name(profile.dpad.mode);
    tiles_[LStick].mode_text  = stick_mode_name(profile.left_stick.mode);
    tiles_[RStick].mode_text  = stick_mode_name(profile.right_stick.mode);
    tiles_[Gyro].mode_text    = gyro_mode_name(profile.gyro.mode);
    tiles_[LPad].mode_text    = touchpad_mode_name(profile.left_touchpad.mode);
    tiles_[RPad].mode_text    = touchpad_mode_name(profile.right_touchpad.mode);
    refresh_params(profile);
    invalidate();
}

void QuickAccessOverlay::refresh_params(const config::Profile& profile) {
    // Called under tiles_mtx_ lock
    params_[0].value = profile.left_touchpad.sensitivity;
    params_[0].value_text = fmt_float(params_[0].value, 1);
    params_[1].value = profile.left_touchpad.inertia;
    params_[1].value_text = fmt_float(params_[1].value, 2);
    params_[2].value_text = accel_name(profile.left_touchpad.acceleration);
    params_[3].value_text = curve_name(profile.left_stick.curve);
    params_[4].value = profile.overlap;
    params_[4].value_text = fmt_float(params_[4].value, 2);
}

void QuickAccessOverlay::on_show(WPARAM wp) {
    reposition();
    OverlayBase::on_show(wp);
}

void QuickAccessOverlay::reposition() {
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int x = (sw - kTotalW) / 2;
    int y = (sh - kTotalH) / 2;
    SetWindowPos(hwnd(), HWND_TOPMOST, x, y, kTotalW, kTotalH,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

HRESULT QuickAccessOverlay::create_child_resources() {
    if (!dwrite_ || !rt_) return E_FAIL;

    // Small label text (16pt)
    HRESULT hr = dwrite_->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        16.0f, L"en-US", &label_fmt_);
    if (FAILED(hr)) return hr;
    label_fmt_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    label_fmt_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    // Large mode text (21pt bold)
    hr = dwrite_->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        21.0f, L"en-US", &mode_fmt_);
    if (FAILED(hr)) return hr;
    mode_fmt_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    mode_fmt_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    hr = rt_->CreateSolidColorBrush(D2D1::ColorF(0.10f, 0.10f, 0.10f), &br_slider_bg_);
    if (FAILED(hr)) return hr;

    return S_OK;
}

void QuickAccessOverlay::release_child_resources() {
    auto rel = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };
    rel(label_fmt_);
    rel(mode_fmt_);
    rel(br_slider_bg_);
}

void QuickAccessOverlay::on_paint() {
    if (!rt_) {
        if (FAILED(recreate_resources())) {
            ValidateRect(hwnd(), nullptr);
            return;
        }
    }

    rt_->BeginDraw();
    rt_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f));

    // Background panel
    D2D1_ROUNDED_RECT bg;
    bg.rect = D2D1::RectF(2.0f, 2.0f,
                           static_cast<float>(kTotalW) - 2.0f,
                           static_cast<float>(kTotalH) - 2.0f);
    bg.radiusX = 6.0f;
    bg.radiusY = 6.0f;
    rt_->FillRoundedRectangle(bg, br_center_);

    int cur = cursor_.load(std::memory_order_relaxed);

    // Draw tiles
    for (int i = 0; i < kTiles; i++)
        draw_tile(i, cur);

    // Draw params
    for (int i = 0; i < kParams; i++)
        draw_param(i, cur);

    HRESULT hr = rt_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        release_child_resources();
        rt_ = nullptr; // triggers full recreate on next paint
    }
    ValidateRect(hwnd(), nullptr);
}

void QuickAccessOverlay::draw_tile(int index, int cursor) {
    if (!rt_ || !mode_fmt_ || !label_fmt_) return;

    int col = index % kCols;
    int row = index / kCols;

    float x = static_cast<float>(kPad + col * (kTileW + kGap));
    float y = static_cast<float>(kPad + row * (kTileH + kGap));

    D2D1_ROUNDED_RECT tile;
    tile.rect = D2D1::RectF(x, y, x + kTileW, y + kTileH);
    tile.radiusX = 6.0f;
    tile.radiusY = 6.0f;

    ID2D1SolidColorBrush* brush = (index == cursor) ? br_hover_ : br_sector_;
    rt_->FillRoundedRectangle(tile, brush);

    // Icon on the left side of tile
    constexpr float kIconZoneW = 80.0f;
    draw_icon(index, x, y);

    // Text on the right side
    float text_x = x + kIconZoneW;
    float text_w = kTileW - kIconZoneW - 8;

    std::wstring label, mode;
    {
        std::lock_guard lock(tiles_mtx_);
        label = tiles_[index].label;
        mode  = tiles_[index].mode_text;
    }

    // Label (small, top-right, right-aligned)
    D2D1_RECT_F label_rect = D2D1::RectF(text_x, y + 10, x + kTileW - 12, y + 36);
    rt_->DrawText(label.c_str(), static_cast<UINT32>(label.size()),
                  label_fmt_, label_rect, br_text_,
                  D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);

    // Mode text (large bold, bottom-right, right-aligned)
    D2D1_RECT_F mode_rect = D2D1::RectF(text_x, y + 38, x + kTileW - 12, y + kTileH - 8);
    rt_->DrawText(mode.c_str(), static_cast<UINT32>(mode.size()),
                  mode_fmt_, mode_rect, br_text_,
                  D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
}

void QuickAccessOverlay::draw_icon(int index, float x, float y) {
    if (!rt_ || !br_text_) return;

    // Icon centered in left 80px zone of tile
    constexpr float kIconZoneW = 80.0f;
    float cx = x + kIconZoneW / 2.0f;
    float cy = y + kTileH / 2.0f;

    switch (static_cast<TileId>(index)) {
        case DPad: {
            // Cross: 5 squares (center + 4 directions)
            // Cross shape ~48px total (matches stick circle diameter of 48)
            float a = 7.0f;  // arm half-width
            float L = 24.0f; // arm length from center
            rt_->FillRectangle(D2D1::RectF(cx - a, cy - L, cx + a, cy + L), br_text_); // vertical
            rt_->FillRectangle(D2D1::RectF(cx - L, cy - a, cx + L, cy + a), br_text_); // horizontal
            break;
        }
        case LStick:
        case RStick: {
            // Circle outline + center dot
            float r = 24.0f;
            rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), br_text_, 2.5f);
            rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 6.0f, 6.0f), br_text_);
            break;
        }
        case LPad:
        case RPad: {
            // Rounded rectangle outline (touchpad shape)
            D2D1_ROUNDED_RECT rr;
            rr.rect = D2D1::RectF(cx - 30, cy - 21, cx + 30, cy + 21);
            rr.radiusX = 6.0f;
            rr.radiusY = 6.0f;
            rt_->DrawRoundedRectangle(rr, br_text_, 2.5f);
            break;
        }
        case Gyro: {
            // Three concentric arcs (gyro/motion symbol)
            for (int i = 0; i < 3; i++) {
                float r = 12.0f + i * 10.0f;
                D2D1_POINT_2F start = {cx + r * cosf(-0.8f), cy + r * sinf(-0.8f)};
                D2D1_POINT_2F end   = {cx + r * cosf(0.8f),  cy + r * sinf(0.8f)};
                ID2D1PathGeometry* geom = nullptr;
                if (SUCCEEDED(factory_->CreatePathGeometry(&geom))) {
                    ID2D1GeometrySink* sink = nullptr;
                    if (SUCCEEDED(geom->Open(&sink))) {
                        sink->BeginFigure(start, D2D1_FIGURE_BEGIN_HOLLOW);
                        D2D1_ARC_SEGMENT arc{};
                        arc.point = end;
                        arc.size = {r, r};
                        arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
                        arc.arcSize = D2D1_ARC_SIZE_SMALL;
                        sink->AddArc(arc);
                        sink->EndFigure(D2D1_FIGURE_END_OPEN);
                        sink->Close();
                        sink->Release();
                        rt_->DrawGeometry(geom, br_text_, 2.5f);
                    }
                    geom->Release();
                }
            }
            break;
        }
    }
}

void QuickAccessOverlay::draw_param(int index, int cursor) {
    if (!rt_ || !mode_fmt_ || !label_fmt_) return;

    float x = static_cast<float>(kPad);
    float y = static_cast<float>(kTilesH + kSepGap + index * (kParamH + kParamGap));
    float w = static_cast<float>(kTotalW - kPad * 2);

    bool selected = (cursor == kTiles + index);

    std::wstring label, value_text;
    ParamType type;
    float norm_value = 0;
    {
        std::lock_guard lock(tiles_mtx_);
        const auto& p = params_[index];
        label = p.label;
        value_text = p.value_text;
        type = p.type;
        if (type == ParamType::Slider && p.max_val > p.min_val)
            norm_value = (p.value - p.min_val) / (p.max_val - p.min_val);
    }

    // Label (left side)
    constexpr float kLabelW = 280.0f;
    ID2D1SolidColorBrush* label_brush = selected ? br_active_ : br_text_;
    D2D1_RECT_F label_rect = D2D1::RectF(x, y + 4, x + kLabelW, y + kParamH - 4);
    rt_->DrawText(label.c_str(), static_cast<UINT32>(label.size()),
                  label_fmt_, label_rect, label_brush,
                  D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);

    // Value area (right side)
    float val_x = x + kLabelW + 8;
    float val_w = w - kLabelW - 8;

    if (type == ParamType::Slider) {
        // Slider bar
        float bar_y = y + kParamH / 2.0f - 4.0f;
        float bar_h = 8.0f;
        float bar_w = val_w - 70;
        D2D1_ROUNDED_RECT bar_bg;
        bar_bg.rect = D2D1::RectF(val_x, bar_y, val_x + bar_w, bar_y + bar_h);
        bar_bg.radiusX = 4.0f;
        bar_bg.radiusY = 4.0f;
        rt_->FillRoundedRectangle(bar_bg, br_slider_bg_);

        float fill_w = bar_w * std::clamp(norm_value, 0.0f, 1.0f);
        if (fill_w > 0.5f) {
            D2D1_ROUNDED_RECT bar_fill;
            bar_fill.rect = D2D1::RectF(val_x, bar_y, val_x + fill_w, bar_y + bar_h);
            bar_fill.radiusX = 4.0f;
            bar_fill.radiusY = 4.0f;
            rt_->FillRoundedRectangle(bar_fill, selected ? br_active_ : br_hover_);
        }

        // Numeric value
        D2D1_RECT_F val_rect = D2D1::RectF(val_x + bar_w + 8, y + 4,
                                             val_x + val_w, y + kParamH - 4);
        rt_->DrawText(value_text.c_str(), static_cast<UINT32>(value_text.size()),
                      mode_fmt_, val_rect, br_text_,
                      D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
    } else {
        // Button: value text
        D2D1_RECT_F val_rect = D2D1::RectF(val_x, y + 4, val_x + val_w, y + kParamH - 4);
        rt_->DrawText(value_text.c_str(), static_cast<UINT32>(value_text.size()),
                      mode_fmt_, val_rect, selected ? br_active_ : br_text_,
                      D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
    }
}

} // namespace windeck::overlay
