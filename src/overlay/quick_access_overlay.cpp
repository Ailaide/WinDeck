#include "overlay/quick_access_overlay.h"
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>

namespace windeck::overlay {

// Custom overlay-thread messages for profile name entry (handled in on_app_message).
static constexpr UINT WM_QAM_KBD_ON      = WM_APP + 20;
static constexpr UINT WM_QAM_KBD_OFF     = WM_APP + 21;

// Active name-entry target for the low-level keyboard hook, plus modifier state.
static std::atomic<QuickAccessOverlay*> s_name_target{nullptr};
static bool s_kb_shift = false;
static bool s_kb_caps  = false;

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

// --- Constants ---

static const wchar_t* kMainTileLabels[] = {
    L"D-Pad", L"L-Stick", L"R-Stick",
    L"Gyro",  L"L-Pad",   L"R-Pad"
};

static const wchar_t* kTopTileLabels[] = { L"LT", L"LB", L"RB", L"RT" };
static const wchar_t* kMiniRow1Labels[] = { L"A", L"B", L"X", L"Y" };
static const wchar_t* kMiniRow2Labels[] = { L"Start", L"Back", L"LS Click", L"RS Click" };
static const wchar_t* kBottomLabels[] = { L"L4", L"L5", L"R4", L"R5" };

// What each remap tile edits, in display order (top 4, mini1 x4, mini2 x4,
// bottom 4 = 16): a button, or a trigger side. LT and RT never map to a ButtonId.
struct RemapTarget {
    bool trigger;              // true: trigger `side`; false: button `bid`
    config::ButtonId bid;
    int side;                  // 0 = LT, 1 = RT
};
static constexpr RemapTarget kRemapTargets[] = {
    // Top: LT, LB, RB, RT
    {true,  config::ButtonId::Count, 0}, {false, config::ButtonId::LB, 0},
    {false, config::ButtonId::RB, 0},    {true,  config::ButtonId::Count, 1},
    // Mini row 1: A, B, X, Y
    {false, config::ButtonId::A, 0}, {false, config::ButtonId::B, 0},
    {false, config::ButtonId::X, 0}, {false, config::ButtonId::Y, 0},
    // Mini row 2: Start, Back, LSClick, RSClick
    {false, config::ButtonId::Start, 0},   {false, config::ButtonId::Back, 0},
    {false, config::ButtonId::LSClick, 0}, {false, config::ButtonId::RSClick, 0},
    // Bottom: L4, L5, R4, R5
    {false, config::ButtonId::L4, 0}, {false, config::ButtonId::L5, 0},
    {false, config::ButtonId::R4, 0}, {false, config::ButtonId::R5, 0},
};

static const wchar_t* kBindingLetters[config::kTriggerBindings] = { L"A", L"B", L"C" };

static const wchar_t* trigger_name(int side) { return side == 0 ? L"LT" : L"RT"; }

static const wchar_t* binding_mode_name(config::BindingMode m) {
    return m == config::BindingMode::Immediate ? L"Immediate" : L"Hold";
}

static const wchar_t* click_name(config::ClickStrength c) {
    switch (c) {
        case config::ClickStrength::None: return L"None";
        case config::ClickStrength::High: return L"High";
        default:                          return L"Low";
    }
}

static config::ClickStrength next_click(config::ClickStrength c) {
    switch (c) {
        case config::ClickStrength::None: return config::ClickStrength::Low;
        case config::ClickStrength::Low:  return config::ClickStrength::High;
        default:                          return config::ClickStrength::None;
    }
}

// Descriptions per zone
static const wchar_t* kMainDescriptions[] = {
    L"Physical D-pad output. D-Pad = Xbox D-pad, WASD = keyboard WASD keys, Arrows = keyboard arrow keys.",
    L"Left analog stick output. Stick = Xbox left stick, WASD/Arrows = keyboard directional, Mouse = mouse cursor.",
    L"Right analog stick output. Stick = Xbox right stick, WASD/Arrows = keyboard directional, Mouse = mouse cursor.",
    L"Gyroscope aiming. Mouse = move mouse cursor by tilting the device. Disabled = gyro inactive.",
    L"Left touchpad output. Trackball/Mouse = cursor, WASD/Arrows = keys, Radial = radial menu, D-Pad = buttons, Scroll = wheel.",
    L"Right touchpad output. Same modes as left touchpad. Typically used for mouse aiming or radial menus.",
};

// --- Helpers ---

static std::wstring fmt_float(float v, int decimals) {
    wchar_t buf[32];
    swprintf(buf, 32, decimals == 1 ? L"%.1f" : L"%.2f", v);
    return buf;
}

// A 0..1 value as a whole percentage ("30%").
static std::wstring fmt_percent(float v) {
    wchar_t buf[16];
    swprintf(buf, 16, L"%d%%", static_cast<int>(std::lround(v * 100.0f)));
    return buf;
}

// Friendly name for a keyboard VK code (covers every key the picker can bind).
static std::wstring keyboard_key_label(uint16_t vk) {
    wchar_t buf[16];
    if (vk >= '0' && vk <= '9') { swprintf(buf, 16, L"%c", static_cast<char>(vk)); return buf; }
    if (vk >= 'A' && vk <= 'Z') { swprintf(buf, 16, L"%c", static_cast<char>(vk)); return buf; }
    if (vk >= VK_F1 && vk <= VK_F12) { swprintf(buf, 16, L"F%d", vk - VK_F1 + 1); return buf; }
    switch (vk) {
        case VK_SPACE:      return L"Space";
        case VK_RETURN:     return L"Enter";
        case VK_ESCAPE:     return L"Esc";
        case VK_TAB:        return L"Tab";
        case VK_BACK:       return L"Backspace";
        case VK_CAPITAL:    return L"Caps";
        case VK_LSHIFT:     return L"L-Shift";
        case VK_RSHIFT:     return L"R-Shift";
        case VK_LCONTROL:   return L"L-Ctrl";
        case VK_RCONTROL:   return L"R-Ctrl";
        case VK_LMENU:      return L"L-Alt";
        case VK_RMENU:      return L"R-Alt";
        case VK_LWIN:       return L"L-Win";
        case VK_RWIN:       return L"R-Win";
        case VK_APPS:       return L"Menu";
        case VK_SNAPSHOT:   return L"PrtSc";
        case VK_SCROLL:     return L"ScrLk";
        case VK_PAUSE:      return L"Pause";
        case VK_INSERT:     return L"Insert";
        case VK_DELETE:     return L"Delete";
        case VK_HOME:       return L"Home";
        case VK_END:        return L"End";
        case VK_PRIOR:      return L"PgUp";
        case VK_NEXT:       return L"PgDn";
        case VK_UP:         return L"Up";
        case VK_DOWN:       return L"Down";
        case VK_LEFT:       return L"Left";
        case VK_RIGHT:      return L"Right";
        case VK_OEM_1:      return L";";
        case VK_OEM_PLUS:   return L"=";
        case VK_OEM_COMMA:  return L",";
        case VK_OEM_MINUS:  return L"-";
        case VK_OEM_PERIOD: return L".";
        case VK_OEM_2:      return L"/";
        case VK_OEM_3:      return L"`";
        case VK_OEM_4:      return L"[";
        case VK_OEM_5:      return L"\\";
        case VK_OEM_6:      return L"]";
        case VK_OEM_7:      return L"'";
    }
    swprintf(buf, 16, L"0x%X", vk);
    return buf;
}

// --- Action display ---

std::wstring QuickAccessOverlay::action_display(const config::ButtonAction& a) const {
    switch (a.type) {
        case config::ActionType::XboxButton: {
            if (a.code == 0x1000) return L"Xbox A";
            if (a.code == 0x2000) return L"Xbox B";
            if (a.code == 0x4000) return L"Xbox X";
            if (a.code == 0x8000) return L"Xbox Y";
            if (a.code == 0x0100) return L"Xbox LB";
            if (a.code == 0x0200) return L"Xbox RB";
            if (a.code == 0x0010) return L"Xbox Start";
            if (a.code == 0x0020) return L"Xbox Back";
            if (a.code == 0x0040) return L"Xbox LS";
            if (a.code == 0x0080) return L"Xbox RS";
            return L"Xbox ?";
        }
        case config::ActionType::Keyboard:
            return keyboard_key_label(a.code);
        case config::ActionType::MouseButton: {
            if (a.code == 0) return L"Mouse L";
            if (a.code == 1) return L"Mouse R";
            if (a.code == 2) return L"Mouse M";
            return L"Mouse ?";
        }
        case config::ActionType::MouseScroll:
            return a.code == 1 ? L"Scroll Up" : L"Scroll Down";
        case config::ActionType::None:
            return L"None";
    }
    return L"?";
}

// Trigger tile text: "Trigger" in Xbox mode, else bindings A, B and C in short
// form ("RMB · – · LMB").
std::wstring QuickAccessOverlay::trigger_display(const config::TriggerConfig& t) const {
    if (t.mode == config::TriggerMode::Xbox) return L"Trigger";
    std::wstring out;
    for (size_t i = 0; i < config::kTriggerBindings; i++) {
        const auto& a = t.bindings[i].action;
        std::wstring s;
        switch (a.type) {
            case config::ActionType::None:        s = L"–"; break;
            case config::ActionType::Keyboard:    s = keyboard_key_label(a.code); break;
            case config::ActionType::MouseButton: s = a.code == 0 ? L"LMB" : a.code == 1 ? L"RMB" : L"MMB"; break;
            case config::ActionType::MouseScroll: s = a.code == 1 ? L"Whl↑" : L"Whl↓"; break;
            case config::ActionType::XboxButton: {
                s = action_display(a);
                if (s.rfind(L"Xbox ", 0) == 0) s = s.substr(5);
                break;
            }
        }
        if (i > 0) out += L" · ";
        out += s;
    }
    return out;
}

// --- Remap picker: static data ---

static constexpr float kPickerTabH = 40.0f;
static const wchar_t* kPickerTabLabels[3] = { L"Mouse", L"Keyboard", L"XBOX Controller" };

// Display name of a remappable button, for the picker description line.
static const wchar_t* button_id_name(config::ButtonId id) {
    switch (id) {
        case config::ButtonId::A:       return L"A";
        case config::ButtonId::B:       return L"B";
        case config::ButtonId::X:       return L"X";
        case config::ButtonId::Y:       return L"Y";
        case config::ButtonId::LB:      return L"LB";
        case config::ButtonId::RB:      return L"RB";
        case config::ButtonId::Start:   return L"Start";
        case config::ButtonId::Back:    return L"Back";
        case config::ButtonId::LSClick: return L"LS Click";
        case config::ButtonId::RSClick: return L"RS Click";
        case config::ButtonId::L4:      return L"L4";
        case config::ButtonId::L5:      return L"L5";
        case config::ButtonId::R4:      return L"R4";
        case config::ButtonId::R5:      return L"R5";
        default:                        return L"the button";
    }
}

// --- QuickAccessOverlay implementation ---

QuickAccessOverlay::QuickAccessOverlay(windeck::config::Config& config,
                                       windeck::mapper::Mapper& mapper,
                                       windeck::output::SyntheticInput& synth)
    : OverlayBase(L"WinDeckQuickAccess", kTotalW, kTotalH)
    , config_(config), mapper_(mapper), synth_(synth) {
    for (int i = 0; i < kTiles; i++)
        tiles_[i].label = kMainTileLabels[i];

    for (auto& s : remap_display_) s = L"None";

    build_picker_targets();

    start();
}

QuickAccessOverlay::~QuickAccessOverlay() {
    // Ensure the low-level keyboard hook is removed on teardown. Post to the
    // overlay thread (the hook's owner) so kbd_hook_ is only ever touched there;
    // the message drains before ~OverlayBase posts WM_CLOSE. If the process is
    // torn down first, the OS reclaims the global hook.
    s_name_target.store(nullptr, std::memory_order_relaxed);
    if (hwnd()) PostMessageW(hwnd(), WM_QAM_KBD_OFF, 0, 0);
}

void QuickAccessOverlay::show() {
    visible_.store(true, std::memory_order_relaxed);
    cursor_.store(kZoneProfiles, std::memory_order_relaxed); // start on the profiles dropdown
    submenu_open_.store(false, std::memory_order_relaxed);
    dropdown_open_.store(false, std::memory_order_relaxed);
    picker_open_.store(false, std::memory_order_relaxed);
    picker_from_submenu_.store(false, std::memory_order_relaxed);
    refresh_profiles();
    post_show();
}

void QuickAccessOverlay::hide() {
    // A name already accepted with Enter is applied before the menu closes (and
    // before the close-time save); an entry still open is cancelled.
    process_name_entry();
    if (name_entry_.load(std::memory_order_relaxed)) cancel_name_entry();
    visible_.store(false, std::memory_order_relaxed);
    submenu_open_.store(false, std::memory_order_relaxed);
    dropdown_open_.store(false, std::memory_order_relaxed);
    picker_open_.store(false, std::memory_order_relaxed);
    picker_from_submenu_.store(false, std::memory_order_relaxed);
    post_hide();
}

// --- Navigation ---

void QuickAccessOverlay::navigate(int dx, int dy) {
    if (name_entry_.load(std::memory_order_relaxed)) return;
    if (picker_open_.load(std::memory_order_relaxed)) { picker_navigate(dx, dy); return; }
    if (dropdown_open_.load(std::memory_order_relaxed)) {
        navigate_dropdown(dx, dy);
        return;
    }
    if (submenu_open_.load(std::memory_order_relaxed)) {
        navigate_submenu(dx, dy);
        return;
    }
    navigate_main(dx, dy);
}

void QuickAccessOverlay::navigate_main(int dx, int dy) {
    int cur = cursor_.load(std::memory_order_relaxed);

    // Determine which zone and position within zone
    struct ZoneInfo { int start; int cols; };
    ZoneInfo zones[] = {
        {kZoneProfiles, kProfileItems},  // 0: profile bar (dropdown, New, Delete)
        {kZoneTop,    kTopTiles},        // 1: top row (2: LB, RB)
        {kZoneMain,   kCols},            // 2: main row 0 (3)
        {kZoneMain+3, kCols},            // 3: main row 1 (3)
        {kZoneMini1,  kMiniRow1},        // 4: mini row 1 (4)
        {kZoneMini2,  kMiniRow2},        // 5: mini row 2 (4)
        {kZoneBottom, kBottomTiles},     // 6: bottom row (4)
    };
    constexpr int kNumZones = 7;

    int zone_idx = -1;
    int col = 0;
    for (int z = 0; z < kNumZones; z++) {
        if (cur >= zones[z].start && cur < zones[z].start + zones[z].cols) {
            zone_idx = z;
            col = cur - zones[z].start;
            break;
        }
    }
    if (zone_idx < 0) return;

    if (dy != 0) {
        int new_zone = zone_idx + dy;
        if (new_zone < 0) new_zone = kNumZones - 1;
        if (new_zone >= kNumZones) new_zone = 0;
        int new_col = (std::min)(col, zones[new_zone].cols - 1);
        cursor_.store(zones[new_zone].start + new_col, std::memory_order_relaxed);
        invalidate();
    }
    if (dx != 0) {
        int new_col = (col + dx + zones[zone_idx].cols) % zones[zone_idx].cols;
        cursor_.store(zones[zone_idx].start + new_col, std::memory_order_relaxed);
        invalidate();
    }
}

void QuickAccessOverlay::activate() {
    if (picker_open_.load(std::memory_order_relaxed)) { picker_bind_current(); return; }
    if (name_entry_.load(std::memory_order_relaxed)) {
        name_queue_.accept();
        process_name_entry();
        return;
    }
    if (dropdown_open_.load(std::memory_order_relaxed)) {
        select_dropdown();
        return;
    }
    if (submenu_open_.load(std::memory_order_relaxed)) {
        activate_submenu();
        return;
    }

    int cur = cursor_.load(std::memory_order_relaxed);

    // Profile bar: open dropdown / New / Delete
    if (cur >= kZoneProfiles && cur < kZoneProfiles + kProfileItems) {
        activate_profile_item(cur - kZoneProfiles);
        return;
    }

    // Default profile is immutable — tiles are read-only.
    if (default_active_.load(std::memory_order_relaxed)) return;

    // Main tiles: cycle mode
    if (cur >= kZoneMain && cur < kZoneMain + kTiles) {
        auto profile = config_.get_profile();
        int ti = cur - kZoneMain;
        switch (static_cast<MainTileId>(ti)) {
            case DPad:   profile.dpad.mode = next_dpad_mode(profile.dpad.mode); break;
            case LStick: profile.left_stick.mode = next_stick_mode(profile.left_stick.mode); break;
            case RStick: profile.right_stick.mode = next_stick_mode(profile.right_stick.mode); break;
            case Gyro:   profile.gyro.mode = next_gyro_mode(profile.gyro.mode); break;
            case LPad:   profile.left_touchpad.mode = next_touchpad_mode(profile.left_touchpad.mode); break;
            case RPad:   profile.right_touchpad.mode = next_touchpad_mode(profile.right_touchpad.mode); break;
        }
        config_.set_profile(profile);
        update_tiles(profile);
        mapper_.apply_profile(profile, synth_);
        return;
    }

    // Trigger tiles: switch between the analog Xbox trigger and Buttons.
    if (int side = trigger_side_for_cursor(cur); side >= 0) {
        auto profile = config_.get_profile();
        auto& t = (side == 0) ? profile.left_trigger : profile.right_trigger;
        t.mode = (t.mode == config::TriggerMode::Xbox) ? config::TriggerMode::Buttons
                                                       : config::TriggerMode::Xbox;
        config_.set_profile(profile);
        update_tiles(profile);
        mapper_.apply_profile(profile, synth_);
        return;
    }

    // Remap tiles: open the remap picker to choose a new binding.
    open_picker();
}

void QuickAccessOverlay::press_x() {
    if (picker_open_.load(std::memory_order_relaxed)) return;
    if (name_entry_.load(std::memory_order_relaxed)) return;
    if (dropdown_open_.load(std::memory_order_relaxed)) return;
    // In the submenu, action bindings are chosen in the picker (opened with A); X is unused.
    if (submenu_open_.load(std::memory_order_relaxed)) return;

    int cur = cursor_.load(std::memory_order_relaxed);

    // Profile bar and immutable Default: X does nothing here.
    if (cur >= kZoneProfiles) return;
    if (default_active_.load(std::memory_order_relaxed)) return;

    // Main tiles: enter submenu
    if (cur >= kZoneMain && cur < kZoneMain + kTiles) {
        enter_submenu();
        return;
    }

    // Trigger tiles: their settings
    if (int side = trigger_side_for_cursor(cur); side >= 0) {
        enter_trigger_submenu(side);
        return;
    }

    // Remap tiles: X is unused (the binding is chosen in the picker via A).
}

void QuickAccessOverlay::hide_submenu() {
    exit_submenu();
}

void QuickAccessOverlay::cycle_category(int direction) {
    if (name_entry_.load(std::memory_order_relaxed)) return;
    if (dropdown_open_.load(std::memory_order_relaxed)) return;
    if (picker_open_.load(std::memory_order_relaxed)) { picker_switch_tab(direction); return; }

    // Remapping (grid buttons and submenu actions) is done in the picker, which switches
    // device tabs on LB/RB. Outside the picker, LB/RB do nothing.
}

// --- Remap helpers ---

config::ButtonId QuickAccessOverlay::button_id_for_cursor(int cursor) const {
    int remap_idx = -1;
    if (cursor >= kZoneTop && cursor < kZoneTop + kTopTiles)
        remap_idx = cursor - kZoneTop;
    else if (cursor >= kZoneMini1 && cursor < kZoneMini1 + kMiniRow1)
        remap_idx = kTopTiles + (cursor - kZoneMini1);
    else if (cursor >= kZoneMini2 && cursor < kZoneMini2 + kMiniRow2)
        remap_idx = kTopTiles + kMiniRow1 + (cursor - kZoneMini2);
    else if (cursor >= kZoneBottom && cursor < kZoneBottom + kBottomTiles)
        remap_idx = kTopTiles + kMiniRow1 + kMiniRow2 + (cursor - kZoneBottom);

    if (remap_idx >= 0 && remap_idx < static_cast<int>(std::size(kRemapTargets)) &&
        !kRemapTargets[remap_idx].trigger)
        return kRemapTargets[remap_idx].bid;
    return config::ButtonId::Count;
}

int QuickAccessOverlay::trigger_side_for_cursor(int cursor) const {
    if (cursor < kZoneTop || cursor >= kZoneTop + kTopTiles) return -1;
    const auto& t = kRemapTargets[cursor - kZoneTop];
    return t.trigger ? t.side : -1;
}

// --- Submenu ---

void QuickAccessOverlay::enter_submenu() {
    int ti = cursor_.load(std::memory_order_relaxed) - kZoneMain;
    if (ti < 0 || ti >= kTiles) return;

    auto profile = config_.get_profile();
    std::lock_guard lock(tiles_mtx_);
    build_sub_params(ti, profile);
    if (sub_params_.empty()) return;

    submenu_tile_ = ti;
    submenu_trigger_ = false;
    sub_cursor_ = 0;
    submenu_open_.store(true, std::memory_order_relaxed);

    // Post resize to overlay thread (wp=new_width)
    PostMessage(hwnd(), WM_OVR_RESIZE, kTotalW + kSubmenuW, 0);
}

void QuickAccessOverlay::enter_trigger_submenu(int side) {
    auto profile = config_.get_profile();
    std::lock_guard lock(tiles_mtx_);
    build_trigger_params(side, profile);
    if (sub_params_.empty()) return;

    submenu_trigger_ = true;
    submenu_side_ = side;
    sub_cursor_ = 0;
    submenu_open_.store(true, std::memory_order_relaxed);
    PostMessage(hwnd(), WM_OVR_RESIZE, kTotalW + kSubmenuW, 0);
}

void QuickAccessOverlay::rebuild_submenu(const config::Profile& profile) {
    // Called with tiles_mtx_ held.
    if (submenu_trigger_) build_trigger_params(submenu_side_, profile);
    else                  build_sub_params(submenu_tile_, profile);
    if (sub_cursor_ >= static_cast<int>(sub_params_.size()))
        sub_cursor_ = sub_params_.empty() ? 0 : static_cast<int>(sub_params_.size()) - 1;
}

void QuickAccessOverlay::exit_submenu() {
    submenu_open_.store(false, std::memory_order_relaxed);

    // Post resize to overlay thread (wp=original_width)
    PostMessage(hwnd(), WM_OVR_RESIZE, kTotalW, 0);
}

bool QuickAccessOverlay::on_app_message(UINT msg, WPARAM wp, LPARAM /*lp*/) {
    if (msg == WM_OVR_RESIZE) {
        int new_w = static_cast<int>(wp);
        set_size(new_w, kTotalH);
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        int x = (sw - new_w) / 2;
        int y = (sh - kTotalH) / 2;
        SetWindowPos(hwnd(), HWND_TOPMOST, x, y, new_w, kTotalH, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        if (rt_) rt_->Resize(D2D1::SizeU(new_w, kTotalH));
        invalidate();
        return true;
    }
    if (msg == WM_QAM_KBD_ON) {
        s_kb_shift = false;
        s_kb_caps = (GetKeyState(VK_CAPITAL) & 1) != 0;
        if (!kbd_hook_)
            kbd_hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, &QuickAccessOverlay::kbd_hook_proc,
                                          GetModuleHandleW(nullptr), 0);
        return true;
    }
    if (msg == WM_QAM_KBD_OFF) {
        if (kbd_hook_) { UnhookWindowsHookEx(kbd_hook_); kbd_hook_ = nullptr; }
        return true;
    }
    return false;
}

// --- Profile bar ---

static std::wstring to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

static std::string to_utf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                        s.data(), n, nullptr, nullptr);
    return s;
}

void QuickAccessOverlay::refresh_profiles() {
    auto files = config_.list_profiles();
    std::string active = config_.active_name();

    // "Default" is synthetic (built-in values, never a file). Drop any stray
    // file named "Default" and always present it as the first entry.
    files.erase(std::remove(files.begin(), files.end(),
                            std::string(config::Config::kDefaultProfileName)),
                files.end());
    std::sort(files.begin(), files.end());

    std::vector<std::string> names;
    names.push_back(config::Config::kDefaultProfileName);
    for (auto& f : files) names.push_back(f);

    default_active_.store(active == config::Config::kDefaultProfileName, std::memory_order_relaxed);

    std::lock_guard lock(tiles_mtx_);
    dd_names_.clear();
    dd_active_ = 0;
    for (size_t i = 0; i < names.size(); i++) {
        dd_names_.push_back(to_wide(names[i]));
        if (names[i] == active) dd_active_ = static_cast<int>(i);
    }
    dd_cursor_ = dd_active_;
}

void QuickAccessOverlay::apply_active_profile() {
    auto profile = config_.get_profile();
    mapper_.apply_profile(profile, synth_);
    update_tiles(profile);   // also refreshes remap_display_ via refresh_all_locked
    refresh_profiles();
}

void QuickAccessOverlay::navigate_dropdown(int /*dx*/, int dy) {
    if (dy == 0) return;
    {
        std::lock_guard lock(tiles_mtx_);
        int n = static_cast<int>(dd_names_.size());
        if (n == 0) return;
        dd_cursor_ = (dd_cursor_ + dy + n) % n;
    }
    invalidate();
}

void QuickAccessOverlay::select_dropdown() {
    std::string name;
    {
        std::lock_guard lock(tiles_mtx_);
        if (dd_cursor_ >= 0 && dd_cursor_ < static_cast<int>(dd_names_.size()))
            name = to_utf8(dd_names_[dd_cursor_]);
    }
    dropdown_open_.store(false, std::memory_order_relaxed);
    if (!name.empty()) {
        config_.save_profile();   // persist edits to the outgoing profile (no-op for Default)
        if (config_.load_profile(name)) apply_active_profile();
    }
    invalidate();
}

void QuickAccessOverlay::close_dropdown() {
    dropdown_open_.store(false, std::memory_order_relaxed);
    invalidate();
}

void QuickAccessOverlay::activate_profile_item(int item) {
    if (item == 0) {
        // Open the dropdown list, starting on the active profile.
        {
            std::lock_guard lock(tiles_mtx_);
            dd_cursor_ = dd_active_;
        }
        dropdown_open_.store(true, std::memory_order_relaxed);
        invalidate();
    } else if (item == 1) {
        // New: prompt for a name, then create a profile from the standard Default.
        begin_name_entry();
    } else if (item == 2) {
        // Delete the active profile (never Default); config falls back to Default.
        std::string active = config_.active_name();
        if (config_.is_default_name(active)) { invalidate(); return; }
        if (config_.delete_profile(active)) {
            apply_active_profile();
        }
        invalidate();
    }
}

// --- Profile name entry ---

static std::string sanitize_profile_name(const std::string& in) {
    std::string out;
    for (char c : in) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') continue;
        if (static_cast<unsigned char>(c) < 0x20) continue;
        out.push_back(c);
    }
    size_t a = out.find_first_not_of(' ');
    if (a == std::string::npos) return "";
    size_t b = out.find_last_not_of(' ');
    return out.substr(a, b - a + 1);
}

void QuickAccessOverlay::begin_name_entry() {
    if (name_entry_.exchange(true, std::memory_order_relaxed)) return;
    name_queue_.begin();
    s_name_target.store(this, std::memory_order_relaxed);
    // Queue show() before WM_QAM_KBD_ON installs the hook: the hook can end the
    // entry on the overlay thread, and that hide() must come after this show().
    if (touch_kbd_) touch_kbd_->show();
    PostMessageW(hwnd(), WM_QAM_KBD_ON, 0, 0);
    invalidate();
}

// Input thread only (via process_name_entry): ends the entry and, on commit,
// creates and applies the profile named `wname`.
void QuickAccessOverlay::end_name_entry(bool commit, const std::wstring& wname) {
    if (!name_entry_.exchange(false, std::memory_order_relaxed)) return;
    s_name_target.store(nullptr, std::memory_order_relaxed);
    PostMessageW(hwnd(), WM_QAM_KBD_OFF, 0, 0);
    if (touch_kbd_) touch_kbd_->hide();

    if (commit) {
        std::string name = config_.unique_name(sanitize_profile_name(to_utf8(wname)));
        config_.save_profile();   // persist edits to the outgoing profile (no-op for Default)
        if (config_.create_from_default(name)) {
            apply_active_profile();
        }
    }
    invalidate();
}

void QuickAccessOverlay::cancel_name_entry() {
    // An accepted name that is still waiting wins over this cancel.
    name_queue_.cancel();
    process_name_entry();
}

void QuickAccessOverlay::process_name_entry() {
    if (auto outcome = name_queue_.take())
        end_name_entry(outcome->commit, outcome->name);
}

void QuickAccessOverlay::name_append(wchar_t c) {
    name_queue_.append(c);
    invalidate();
}

void QuickAccessOverlay::name_backspace() {
    name_queue_.backspace();
    invalidate();
}

LRESULT CALLBACK QuickAccessOverlay::kbd_hook_proc(int code, WPARAM wp, LPARAM lp) {
    auto* target = s_name_target.load(std::memory_order_relaxed);
    if (code == HC_ACTION && target) {
        auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lp);
        if (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN) {
            DWORD vk = kb->vkCode;
            // Injected Unicode (touch keyboard) arrives as VK_PACKET with the
            // UTF-16 code unit in scanCode; ToUnicodeEx cannot translate it.
            if (vk == VK_PACKET) {
                wchar_t ch = static_cast<wchar_t>(kb->scanCode);
                if (ch >= 0x20) target->name_append(ch);
                return 1;
            }
            if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) { s_kb_shift = true; return 1; }
            if (vk == VK_CAPITAL) { s_kb_caps = !s_kb_caps; return 1; }
            // Enter / Escape: record the decision now, with the text as typed so
            // far; the input thread applies it (process_name_entry), as it owns
            // profile changes.
            if (vk == VK_RETURN)  { target->name_queue_.accept(); return 1; }
            if (vk == VK_ESCAPE)  { target->name_queue_.cancel(); return 1; }
            if (vk == VK_BACK)    { target->name_backspace(); return 1; }

            BYTE ks[256] = {};
            if (s_kb_shift) ks[VK_SHIFT]   = 0x80;
            if (s_kb_caps)  ks[VK_CAPITAL] = 0x01;
            wchar_t buf[8] = {};
            // Translate with the foreground window's keyboard layout so the active
            // input language (e.g. Russian) is honored. Flag 0x4 leaves the kernel
            // keyboard state untouched (no dead-key leak into the game).
            HWND fg = GetForegroundWindow();
            DWORD tid = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
            HKL layout = GetKeyboardLayout(tid);
            int n = ToUnicodeEx(vk, kb->scanCode, ks, buf, 8, 0x4, layout);
            for (int i = 0; i < n; i++)
                if (buf[i] >= 0x20) target->name_append(buf[i]);
            return 1;  // swallow so keystrokes never reach the game
        }
        if (wp == WM_KEYUP || wp == WM_SYSKEYUP) {
            DWORD vk = kb->vkCode;
            if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) s_kb_shift = false;
            return 1;
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

void QuickAccessOverlay::navigate_submenu(int dx, int dy) {
    std::lock_guard lock(tiles_mtx_);
    int count = static_cast<int>(sub_params_.size());
    if (count == 0) return;

    if (dy != 0) {
        sub_cursor_ = (sub_cursor_ + dy + count) % count;
        invalidate();
    }

    if (dx != 0) {
        auto& p = sub_params_[sub_cursor_];
        if (p.type == ParamType::Slider) {
            // Steps from the current value, so a depth off the grid (set in the
            // JSON) keeps its offset rather than snapping.
            p.value = std::clamp(p.value + dx * p.step, p.min_val, p.max_val);
            int dec = (p.step < 0.05f) ? 2 : 1;
            p.value_text = p.percent ? fmt_percent(p.value) : fmt_float(p.value, dec);
            apply_sub_param(sub_cursor_);
            invalidate();
        }
    }
}

void QuickAccessOverlay::activate_submenu() {
    // Read the selected param under the lock, then act (the picker path releases the
    // lock first so it can post a resize / touch config without holding tiles_mtx_).
    RemapDest dest = RemapDest::Button;
    int sector = 0;
    int target = 0;
    bool open_picker = false;
    {
        std::lock_guard lock(tiles_mtx_);
        if (sub_cursor_ < 0 || sub_cursor_ >= static_cast<int>(sub_params_.size())) return;
        auto& p = sub_params_[sub_cursor_];
        target = submenu_trigger_ ? submenu_side_ : submenu_tile_;

        if (p.type == ParamType::Button) {
            cycle_sub_button(sub_cursor_);
            invalidate();
            return;
        } else if (p.type == ParamType::Toggle) {
            p.value = (p.value == 0.0f) ? 1.0f : 0.0f;
            p.value_text = (p.value != 0.0f) ? L"On" : L"Off";
            apply_sub_param(sub_cursor_);
            invalidate();
            return;
        } else if (p.type == ParamType::Remap) {
            dest = p.remap_dest;
            sector = p.remap_sector;
            open_picker = true;
        }
    }
    if (open_picker)
        open_picker_submenu(dest, target, sector);
}

void QuickAccessOverlay::build_trigger_params(int side, const config::Profile& profile) {
    // Called with tiles_mtx_ held.
    sub_params_.clear();
    sub_descs_.clear();
    const auto& t = (side == 0) ? profile.left_trigger : profile.right_trigger;

    if (t.mode == config::TriggerMode::Xbox) {
        sub_header_ = std::wstring(trigger_name(side)) + L" > Xbox trigger";
        ParamData p{L"Dead zone", fmt_percent(t.deadzone), ParamType::Slider,
                    t.deadzone, 0.0f, config::kTriggerDeadzoneMax, 0.01f};
        p.percent = true;
        sub_params_.push_back(p);
        sub_descs_.push_back(L"Pull below this sends nothing to the game; above it, the analog trigger starts.");
        return;
    }

    sub_header_ = std::wstring(trigger_name(side)) + L" > Buttons";
    for (size_t i = 0; i < config::kTriggerBindings; i++) {
        const auto& b = t.bindings[i];
        const std::wstring letter = kBindingLetters[i];
        const int bi = static_cast<int>(i);

        ParamData action{L"Binding " + letter, action_display(b.action), ParamType::Remap,
                         0, 0, 0, 0, RemapDest::TriggerBinding, bi};
        action.trigger_binding = bi;
        sub_params_.push_back(action);
        sub_descs_.push_back(L"What binding " + letter + L" sends. Press A to choose; Y in the picker clears it.");

        ParamData pull{letter + L" pull", fmt_percent(b.pull), ParamType::Slider,
                       b.pull, config::kTriggerPullMin, config::kTriggerPullMax, 0.05f};
        pull.trigger_binding = bi;
        pull.percent = true;
        sub_params_.push_back(pull);
        sub_descs_.push_back(L"How far to pull before binding " + letter +
                             L" acts. Each binding has its own depth.");

        ParamData mode{letter + L" mode", binding_mode_name(b.mode), ParamType::Button, 0, 0, 0, 0};
        mode.trigger_binding = bi;
        sub_params_.push_back(mode);
        sub_descs_.push_back(L"Hold keeps the action pressed while pulled past the depth. "
                             L"Immediate taps it once, with nothing on release.");

        ParamData click{letter + L" click", click_name(b.click), ParamType::Button, 0, 0, 0, 0};
        click.trigger_binding = bi;
        sub_params_.push_back(click);
        sub_descs_.push_back(L"Haptic click on this side: None, Low or High. Hold clicks on press and "
                             L"release, Immediate on press only.");
    }
}

void QuickAccessOverlay::build_sub_params(int main_tile, const config::Profile& profile) {
    sub_params_.clear();
    sub_descs_.clear();

    auto add_slider = [&](const wchar_t* label, float val, float mn, float mx, float step, const wchar_t* desc) {
        int dec = (step < 0.05f) ? 2 : 1;
        sub_params_.push_back({label, fmt_float(val, dec), ParamType::Slider, val, mn, mx, step});
        sub_descs_.push_back(desc);
    };
    auto add_button = [&](const wchar_t* label, const wchar_t* val, const wchar_t* desc) {
        sub_params_.push_back({label, val, ParamType::Button, 0, 0, 0, 0});
        sub_descs_.push_back(desc);
    };
    auto add_toggle = [&](const wchar_t* label, bool val, const wchar_t* desc) {
        sub_params_.push_back({label, val ? L"On" : L"Off", ParamType::Toggle, val ? 1.0f : 0.0f, 0, 1, 1});
        sub_descs_.push_back(desc);
    };
    // An action binding, edited via the same picker as the main-menu buttons.
    auto add_remap = [&](const std::wstring& label, const config::ButtonAction& act,
                         RemapDest dest, int sector, const wchar_t* desc) {
        sub_params_.push_back({label, action_display(act), ParamType::Remap, 0, 0, 0, 0, dest, sector});
        sub_descs_.push_back(desc);
    };

    switch (static_cast<MainTileId>(main_tile)) {
        case DPad: {
            auto m = profile.dpad.mode;
            if (m == config::DpadMode::Wasd || m == config::DpadMode::Arrows) {
                add_slider(L"Overlap", profile.dpad.overlap, 0.0f, 1.0f, 0.05f,
                    L"Diagonal overlap. 0 = strict 4-way, 1 = full 8-way diagonal.");
            }
            break;
        }
        case LStick:
        case RStick: {
            const auto& s = (main_tile == LStick) ? profile.left_stick : profile.right_stick;
            add_slider(L"Inner Deadzone", s.inner_deadzone, 0.0f, 0.5f, 0.01f,
                L"Deadzone at stick center. Below this = zero output.");
            add_slider(L"Outer Deadzone", s.outer_deadzone, 0.5f, 1.0f, 0.01f,
                L"Output reaches maximum at this stick deflection.");
            add_button(L"Response Curve", curve_name(s.curve),
                L"Stick response curve. Linear = 1:1, Quadratic = gentle start.");
            if (s.mode == config::StickMode::Mouse) {
                add_slider(L"Mouse Sensitivity", s.mouse_sensitivity, 0.1f, 10.0f, 0.1f,
                    L"Mouse cursor speed multiplier for stick mouse mode.");
            }
            add_toggle(L"Invert X", s.invert_x, L"Invert horizontal axis.");
            add_toggle(L"Invert Y", s.invert_y, L"Invert vertical axis.");
            if (s.mode == config::StickMode::Wasd || s.mode == config::StickMode::Arrows) {
                add_slider(L"Overlap", s.overlap, 0.0f, 1.0f, 0.05f,
                    L"Diagonal overlap. 0 = strict 4-way, 1 = full 8-way diagonal.");
            }
            break;
        }
        case Gyro: {
            if (profile.gyro.mode == config::GyroMode::Mouse) {
                add_slider(L"Sensitivity X", profile.gyro.sensitivity_x, 0.1f, 10.0f, 0.1f,
                    L"Gyro yaw sensitivity (horizontal).");
                add_slider(L"Sensitivity Y", profile.gyro.sensitivity_y, 0.1f, 10.0f, 0.1f,
                    L"Gyro pitch sensitivity (vertical).");
                add_toggle(L"Invert X", profile.gyro.invert_x, L"Invert gyro horizontal.");
                add_toggle(L"Invert Y", profile.gyro.invert_y, L"Invert gyro vertical.");
            }
            break;
        }
        case LPad:
        case RPad: {
            const auto& t = (main_tile == LPad) ? profile.left_touchpad : profile.right_touchpad;
            if (t.mode == config::TouchpadMode::TrackpadInertia) {
                add_slider(L"Sensitivity", t.sensitivity, 0.1f, 5.0f, 0.1f, L"Cursor speed multiplier.");
                add_slider(L"Inertia", t.inertia, 0.80f, 0.99f, 0.01f, L"Momentum decay. Higher = longer glide.");
                add_button(L"Acceleration", accel_name(t.acceleration), L"Off/Normal/Strong acceleration curve.");
                add_toggle(L"Invert X", t.invert_x, L"Invert horizontal cursor.");
                add_toggle(L"Invert Y", t.invert_y, L"Invert vertical cursor.");
                add_remap(L"Click Action", t.click_action, RemapDest::ClickAction, 0,
                    L"Action on touchpad click. Press A to choose a binding.");
            } else if (t.mode == config::TouchpadMode::Mouse) {
                add_slider(L"Sensitivity", t.sensitivity, 0.1f, 5.0f, 0.1f, L"Cursor speed multiplier.");
                add_button(L"Acceleration", accel_name(t.acceleration), L"Off/Normal/Strong acceleration curve.");
                add_toggle(L"Invert X", t.invert_x, L"Invert horizontal cursor.");
                add_toggle(L"Invert Y", t.invert_y, L"Invert vertical cursor.");
                add_remap(L"Click Action", t.click_action, RemapDest::ClickAction, 0,
                    L"Action on touchpad click. Press A to choose a binding.");
            } else if (t.mode == config::TouchpadMode::Wasd || t.mode == config::TouchpadMode::Arrows) {
                add_slider(L"Overlap", t.overlap, 0.0f, 1.0f, 0.05f,
                    L"Diagonal overlap. 0 = strict 4-way, 1 = full 8-way diagonal.");
            } else if (t.mode == config::TouchpadMode::DpadButtons) {
                add_slider(L"Overlap", t.overlap, 0.0f, 1.0f, 0.05f,
                    L"Diagonal overlap for D-pad zones.");
            } else if (t.mode == config::TouchpadMode::RadialMenu) {
                // 8 sector remaps
                const wchar_t* arrows[] = {L"\u2191",L"\u2197",L"\u2192",L"\u2198",L"\u2193",L"\u2199",L"\u2190",L"\u2196"};
                for (int i = 0; i < 8; i++) {
                    std::wstring lbl = std::wstring(arrows[i]) + L" Sector " + std::to_wstring(i + 1);
                    add_remap(lbl, t.radial_keys[i], RemapDest::RadialKey, i,
                        L"Radial sector action. Press A to choose a binding.");
                }
            }
            break;
        }
    }
}

void QuickAccessOverlay::apply_sub_param(int /*param_idx*/) {
    // Write current sub_params_ values back to profile
    auto profile = config_.get_profile();

    if (submenu_trigger_) {
        auto& t = (submenu_side_ == 0) ? profile.left_trigger : profile.right_trigger;
        for (const auto& p : sub_params_) {
            if (p.type != ParamType::Slider) continue;
            if (p.trigger_binding >= 0)
                t.bindings[static_cast<size_t>(p.trigger_binding)].pull = p.value;
            else
                t.deadzone = p.value;
        }
        config_.set_profile(profile);
        mapper_.apply_profile(profile, synth_);
        return;
    }

    auto ti = static_cast<MainTileId>(submenu_tile_);

    // Rebuild from sub_params_ based on tile type
    int si = 0; // slider index tracker
    auto read_slider = [&]() -> float { return (si < static_cast<int>(sub_params_.size())) ? sub_params_[si++].value : 0.0f; };
    auto skip = [&]() { si++; }; // skip button/toggle (handled by cycle)
    auto read_toggle = [&]() -> bool { return (si < static_cast<int>(sub_params_.size())) ? sub_params_[si++].value != 0.0f : false; };

    switch (ti) {
        case DPad:
            if (profile.dpad.mode == config::DpadMode::Wasd || profile.dpad.mode == config::DpadMode::Arrows)
                profile.dpad.overlap = read_slider();
            break;
        case LStick:
        case RStick: {
            auto& s = (ti == LStick) ? profile.left_stick : profile.right_stick;
            s.inner_deadzone = read_slider();
            s.outer_deadzone = read_slider();
            skip(); // curve (button)
            if (s.mode == config::StickMode::Mouse) s.mouse_sensitivity = read_slider();
            s.invert_x = read_toggle();
            s.invert_y = read_toggle();
            if (s.mode == config::StickMode::Wasd || s.mode == config::StickMode::Arrows)
                s.overlap = read_slider();
            break;
        }
        case Gyro:
            if (profile.gyro.mode == config::GyroMode::Mouse) {
                profile.gyro.sensitivity_x = read_slider();
                profile.gyro.sensitivity_y = read_slider();
                profile.gyro.invert_x = read_toggle();
                profile.gyro.invert_y = read_toggle();
            }
            break;
        case LPad:
        case RPad: {
            auto& t = (ti == LPad) ? profile.left_touchpad : profile.right_touchpad;
            if (t.mode == config::TouchpadMode::TrackpadInertia) {
                t.sensitivity = read_slider();
                t.inertia = read_slider();
                skip(); // acceleration (button)
                t.invert_x = read_toggle();
                t.invert_y = read_toggle();
                skip(); // click_action (button)
            } else if (t.mode == config::TouchpadMode::Mouse) {
                t.sensitivity = read_slider();
                skip(); // acceleration
                t.invert_x = read_toggle();
                t.invert_y = read_toggle();
                skip(); // click_action
            } else if (t.mode == config::TouchpadMode::Wasd || t.mode == config::TouchpadMode::Arrows
                       || t.mode == config::TouchpadMode::DpadButtons) {
                t.overlap = read_slider();
            }
            // RadialMenu: handled separately via remap
            break;
        }
    }

    config_.set_profile(profile);
    mapper_.apply_profile(profile, synth_);
}

void QuickAccessOverlay::cycle_sub_button(int param_idx) {
    auto profile = config_.get_profile();
    auto& p = sub_params_[param_idx];

    if (submenu_trigger_) {
        if (p.trigger_binding < 0) return;
        auto& t = (submenu_side_ == 0) ? profile.left_trigger : profile.right_trigger;
        auto& b = t.bindings[static_cast<size_t>(p.trigger_binding)];
        const std::wstring& l = p.label;
        if (l.size() >= 5 && l.compare(l.size() - 5, 5, L" mode") == 0) {
            b.mode = (b.mode == config::BindingMode::Hold) ? config::BindingMode::Immediate
                                                            : config::BindingMode::Hold;
            p.value_text = binding_mode_name(b.mode);
        } else if (l.size() >= 6 && l.compare(l.size() - 6, 6, L" click") == 0) {
            b.click = next_click(b.click);
            p.value_text = click_name(b.click);
        }
        config_.set_profile(profile);
        mapper_.apply_profile(profile, synth_);
        return;
    }

    auto ti = static_cast<MainTileId>(submenu_tile_);

    // Identify which button param by label
    if (p.label == L"Response Curve") {
        auto& s = (ti == LStick) ? profile.left_stick : profile.right_stick;
        s.curve = next_curve(s.curve);
        p.value_text = curve_name(s.curve);
    } else if (p.label == L"Acceleration") {
        auto& t = (ti == LPad) ? profile.left_touchpad : profile.right_touchpad;
        t.acceleration = next_accel(t.acceleration);
        p.value_text = accel_name(t.acceleration);
    }

    config_.set_profile(profile);
    mapper_.apply_profile(profile, synth_);
}

// --- Update ---

void QuickAccessOverlay::update_tiles(const config::Profile& profile) {
    std::lock_guard lock(tiles_mtx_);
    tiles_[DPad].mode_text    = dpad_mode_name(profile.dpad.mode);
    tiles_[LStick].mode_text  = stick_mode_name(profile.left_stick.mode);
    tiles_[RStick].mode_text  = stick_mode_name(profile.right_stick.mode);
    tiles_[Gyro].mode_text    = gyro_mode_name(profile.gyro.mode);
    tiles_[LPad].mode_text    = touchpad_mode_name(profile.left_touchpad.mode);
    tiles_[RPad].mode_text    = touchpad_mode_name(profile.right_touchpad.mode);
    refresh_all_locked(profile);
    invalidate();
}

void QuickAccessOverlay::refresh_all(const config::Profile& profile) {
    std::lock_guard lock(tiles_mtx_);
    refresh_all_locked(profile);
}

void QuickAccessOverlay::refresh_all_locked(const config::Profile& profile) {
    // Must be called with tiles_mtx_ held
    for (int i = 0; i < kRemapTiles; i++) {
        const auto& t = kRemapTargets[i];
        if (t.trigger)
            remap_display_[i] = trigger_display(t.side == 0 ? profile.left_trigger
                                                            : profile.right_trigger);
        else
            remap_display_[i] = action_display(profile.buttons[static_cast<int>(t.bid)]);
    }
}

// --- Show/Reposition ---

void QuickAccessOverlay::on_show(WPARAM wp) {
    reposition();
    OverlayBase::on_show(wp);
}

void QuickAccessOverlay::reposition() {
    // Always open at the base (no-submenu) width. If the menu was previously closed
    // while a submenu was open, hide() left the window and the D2D render target at the
    // wider submenu size; reset both here (matching the WM_OVR_RESIZE handler) so the
    // window size and render target stay in sync and proportions are correct on reopen.
    set_size(kTotalW, kTotalH);
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int x = (sw - kTotalW) / 2;
    int y = (sh - kTotalH) / 2;
    SetWindowPos(hwnd(), HWND_TOPMOST, x, y, kTotalW, kTotalH,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (rt_) rt_->Resize(D2D1::SizeU(kTotalW, kTotalH));
}

// --- D2D Resources ---

HRESULT QuickAccessOverlay::create_child_resources() {
    if (!dwrite_ || !rt_) return E_FAIL;
    HRESULT hr;

    // Small label text (16pt)
    hr = dwrite_->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        16.0f, L"en-US", &label_fmt_);
    if (FAILED(hr)) return hr;
    label_fmt_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    label_fmt_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    // Large mode text (21pt bold)
    hr = dwrite_->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        21.0f, L"en-US", &mode_fmt_);
    if (FAILED(hr)) return hr;
    mode_fmt_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    mode_fmt_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Action bar text (13pt bold)
    hr = dwrite_->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        13.0f, L"en-US", &action_fmt_);
    if (FAILED(hr)) return hr;
    action_fmt_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    action_fmt_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Description text (13pt normal, word wrap)
    hr = dwrite_->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        13.0f, L"en-US", &desc_fmt_);
    if (FAILED(hr)) return hr;
    desc_fmt_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    desc_fmt_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    desc_fmt_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);

    // Mini tile text (12pt bold, centered)
    hr = dwrite_->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        12.0f, L"en-US", &mini_fmt_);
    if (FAILED(hr)) return hr;
    mini_fmt_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    mini_fmt_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Keyboard key labels (11pt bold, centered)
    hr = dwrite_->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        11.0f, L"en-US", &key_fmt_);
    if (FAILED(hr)) return hr;
    key_fmt_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    key_fmt_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    hr = rt_->CreateSolidColorBrush(D2D1::ColorF(0.10f, 0.10f, 0.10f), &br_slider_bg_);
    if (FAILED(hr)) return hr;
    hr = rt_->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.55f, 0.55f, 1.0f), &br_dim_text_);
    if (FAILED(hr)) return hr;
    hr = rt_->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.5f), &br_dim_overlay_);
    if (FAILED(hr)) return hr;

    return S_OK;
}

void QuickAccessOverlay::release_child_resources() {
    auto rel = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };
    rel(label_fmt_);
    rel(mode_fmt_);
    rel(action_fmt_);
    rel(desc_fmt_);
    rel(mini_fmt_);
    rel(key_fmt_);
    rel(br_slider_bg_);
    rel(br_dim_text_);
    rel(br_dim_overlay_);
}

// --- Drawing ---

void QuickAccessOverlay::on_paint() {
    if (!rt_) {
        if (FAILED(recreate_resources())) {
            ValidateRect(hwnd(), nullptr);
            return;
        }
    }

    rt_->BeginDraw();
    rt_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f));

    int cur = cursor_.load(std::memory_order_relaxed);
    float my = menu_top(); // Y offset where menu panel starts

    // Action bar (above menu panel)
    draw_action_bar();

    // Menu panel background
    D2D1_ROUNDED_RECT bg;
    bg.rect = D2D1::RectF(2.0f, my, static_cast<float>(kTotalW) - 2.0f,
                            my + static_cast<float>(kMenuH));
    bg.radiusX = 6.0f;
    bg.radiusY = 6.0f;
    rt_->FillRoundedRectangle(bg, br_center_);

    if (picker_open_.load(std::memory_order_relaxed)) {
        draw_picker();
    } else {
    float y = my + static_cast<float>(kPad);
    float x = static_cast<float>(kPad);

    {
        std::lock_guard lock(tiles_mtx_);

        // Reserve the profile bar row at the top (drawn last so its open
        // dropdown list stacks above the tiles below it).
        float profile_bar_y = y;
        y += kProfileRowH + kProfileGap;

        // --- Top row: LT, LB, RB, RT ---
        float tw = static_cast<float>(kSmallTileW);
        for (int i = 0; i < kTopTiles; i++) {
            float tx = x + i * (tw + kGap);
            draw_small_tile(tx, y, tw, kTopTileLabels[i], remap_display_[i], cur == kZoneTop + i);
        }
        y += kSmallTileH + kGap;

        // --- Main 3x2 tiles ---
        float main_y = y;
        for (int i = 0; i < kTiles; i++) {
            int col = i % kCols;
            int row = i / kCols;
            float tx = static_cast<float>(kPad + col * (kTileW + kGap));
            float ty = main_y + static_cast<float>(row * (kTileH + kGap));

            D2D1_ROUNDED_RECT tile;
            tile.rect = D2D1::RectF(tx, ty, tx + kTileW, ty + kTileH);
            tile.radiusX = 6.0f; tile.radiusY = 6.0f;
            bool selected = (kZoneMain + i == cur);
            rt_->FillRoundedRectangle(tile, selected ? br_hover_ : br_sector_);
            ID2D1SolidColorBrush* tile_br = selected ? br_active_ : br_text_;
            draw_icon(i, tx, ty, tile_br);

            constexpr float kIconZoneW = 80.0f;
            D2D1_RECT_F lr = D2D1::RectF(tx + kIconZoneW, ty + 10, tx + kTileW - 12, ty + 36);
            rt_->DrawText(tiles_[i].label.c_str(), static_cast<UINT32>(tiles_[i].label.size()),
                          label_fmt_, lr, tile_br,
                          D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
            D2D1_RECT_F mr = D2D1::RectF(tx + kIconZoneW, ty + 38, tx + kTileW - 12, ty + kTileH - 8);
            rt_->DrawText(tiles_[i].mode_text.c_str(), static_cast<UINT32>(tiles_[i].mode_text.size()),
                          mode_fmt_, mr, tile_br,
                          D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
        }
        y += kRows * kTileH + (kRows - 1) * kGap + kGap;

        // --- Mini row 1: A, B, X, Y ---
        float mw = (static_cast<float>(kTotalW) - 2 * kPad - 3 * kMiniGap) / 4.0f;
        for (int i = 0; i < kMiniRow1; i++) {
            float mx = static_cast<float>(kPad) + i * (mw + kMiniGap);
            int remap_idx = kTopTiles + i;
            draw_mini_tile(mx, y, mw, kMiniRow1Labels[i], remap_display_[remap_idx], cur == kZoneMini1 + i);
        }
        y += kMiniTileH + kMiniGap;

        // --- Mini row 2: Start, Back, LSC, RSC ---
        for (int i = 0; i < kMiniRow2; i++) {
            float mx = static_cast<float>(kPad) + i * (mw + kMiniGap);
            int remap_idx = kTopTiles + kMiniRow1 + i;
            draw_mini_tile(mx, y, mw, kMiniRow2Labels[i], remap_display_[remap_idx], cur == kZoneMini2 + i);
        }
        y += kMiniTileH + kGap;

        // --- Bottom row: L4, L5, R4, R5 ---
        for (int i = 0; i < kBottomTiles; i++) {
            float bx = static_cast<float>(kPad) + i * (tw + kGap);
            int remap_idx = kTopTiles + kMiniRow1 + kMiniRow2 + i;
            draw_small_tile(bx, y, tw, kBottomLabels[i], remap_display_[remap_idx], cur == kZoneBottom + i);
        }

        // Immutable Default: dim the tile area to show it is read-only.
        if (default_active_.load(std::memory_order_relaxed) &&
            !submenu_open_.load(std::memory_order_relaxed)) {
            float tiles_top = profile_bar_y + kProfileRowH + kProfileGap - 4.0f;
            rt_->FillRectangle(
                D2D1::RectF(2.0f, tiles_top, static_cast<float>(kTotalW) - 2.0f,
                            my + static_cast<float>(kMenuH) - 2.0f),
                br_dim_overlay_);
        }

        // Profile bar (drawn last so the open dropdown list stacks on top).
        draw_profile_bar(profile_bar_y);

        // Submenu overlay (if open)
        if (submenu_open_.load(std::memory_order_relaxed))
            draw_submenu();
    } // lock released
    }

    // Description (below menu panel)
    draw_description();

    HRESULT hr = rt_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        release_child_resources();
        rt_ = nullptr;
    }
    ValidateRect(hwnd(), nullptr);
}

void QuickAccessOverlay::draw_icon(int index, float x, float y, ID2D1SolidColorBrush* brush) {
    if (!rt_ || !brush) return;
    constexpr float kIconZoneW = 80.0f;
    float cx = x + kIconZoneW / 2.0f;
    float cy = y + kTileH / 2.0f;

    switch (static_cast<MainTileId>(index)) {
        case DPad: {
            float a = 7.0f, L = 24.0f;
            rt_->FillRectangle(D2D1::RectF(cx - a, cy - L, cx + a, cy + L), brush);
            rt_->FillRectangle(D2D1::RectF(cx - L, cy - a, cx + L, cy + a), brush);
            break;
        }
        case LStick:
        case RStick: {
            float r = 24.0f;
            rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), brush, 2.5f);
            rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 6.0f, 6.0f), brush);
            break;
        }
        case LPad:
        case RPad: {
            D2D1_ROUNDED_RECT rr;
            rr.rect = D2D1::RectF(cx - 30, cy - 21, cx + 30, cy + 21);
            rr.radiusX = 6.0f; rr.radiusY = 6.0f;
            rt_->DrawRoundedRectangle(rr, brush, 2.5f);
            break;
        }
        case Gyro: {
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
                        rt_->DrawGeometry(geom, brush, 2.5f);
                    }
                    geom->Release();
                }
            }
            break;
        }
    }
}

void QuickAccessOverlay::draw_small_tile(float x, float y, float w,
                                          const std::wstring& label,
                                          const std::wstring& value, bool selected) {
    if (!rt_ || !mini_fmt_ || !action_fmt_) return;
    D2D1_ROUNDED_RECT rr;
    rr.rect = D2D1::RectF(x, y, x + w, y + kSmallTileH);
    rr.radiusX = 6.0f; rr.radiusY = 6.0f;
    rt_->FillRoundedRectangle(rr, selected ? br_hover_ : br_sector_);

    // Same layout as mini tiles: label left, value right
    D2D1_RECT_F lr = D2D1::RectF(x + 8, y, x + w * 0.4f, y + kSmallTileH);
    rt_->DrawText(label.c_str(), static_cast<UINT32>(label.size()),
                  action_fmt_, lr, br_dim_text_,
                  D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
    D2D1_RECT_F vr = D2D1::RectF(x + w * 0.4f, y, x + w - 8, y + kSmallTileH);
    // Clipped: a long value (three trigger bindings, a long key name) never
    // spills out of its tile.
    rt_->DrawText(value.c_str(), static_cast<UINT32>(value.size()),
                  mini_fmt_, vr, selected ? br_active_ : br_text_,
                  D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
}

void QuickAccessOverlay::draw_mini_tile(float x, float y, float w,
                                         const std::wstring& label,
                                         const std::wstring& value, bool selected) {
    if (!rt_ || !mini_fmt_) return;
    D2D1_ROUNDED_RECT rr;
    rr.rect = D2D1::RectF(x, y, x + w, y + kMiniTileH);
    rr.radiusX = 6.0f; rr.radiusY = 6.0f;
    rt_->FillRoundedRectangle(rr, selected ? br_hover_ : br_sector_);

    // Label (left)
    D2D1_RECT_F lr = D2D1::RectF(x + 8, y, x + w * 0.4f, y + kMiniTileH);
    rt_->DrawText(label.c_str(), static_cast<UINT32>(label.size()),
                  action_fmt_, lr, br_dim_text_,
                  D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
    // Value (right)
    D2D1_RECT_F vr = D2D1::RectF(x + w * 0.4f, y, x + w - 8, y + kMiniTileH);
    rt_->DrawText(value.c_str(), static_cast<UINT32>(value.size()),
                  mini_fmt_, vr, selected ? br_active_ : br_text_,
                  D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
}

void QuickAccessOverlay::draw_button_glyph(float cx, float cy, wchar_t letter) {
    if (!rt_ || !text_fmt_) return;
    constexpr float r = 11.0f;
    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), br_text_);
    wchar_t buf[2] = {letter, 0};
    D2D1_RECT_F lr = D2D1::RectF(cx - r, cy - r, cx + r, cy + r);
    rt_->DrawText(buf, 1, text_fmt_, lr, br_center_,
                  D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
}

void QuickAccessOverlay::draw_action_bar() {
    if (!rt_ || !action_fmt_) return;

    // Background bar
    float bar_w = static_cast<float>(submenu_open_.load(std::memory_order_relaxed) ? kTotalW + kSubmenuW : kTotalW);
    D2D1_ROUNDED_RECT bar_bg;
    bar_bg.rect = D2D1::RectF(2.0f, 2.0f, bar_w - 2.0f, static_cast<float>(kActionBarH));
    bar_bg.radiusX = 6.0f; bar_bg.radiusY = 6.0f;
    rt_->FillRoundedRectangle(bar_bg, br_center_);

    float x = static_cast<float>(kPad) + 8.0f;
    float cy = kActionBarH / 2.0f;

    auto draw_text_hint = [&](const wchar_t* text, float w) {
        D2D1_RECT_F r = D2D1::RectF(x, cy - 12, x + w, cy + 12);
        rt_->DrawText(text, static_cast<UINT32>(wcslen(text)), action_fmt_, r, br_text_,
                      D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
        x += w;
    };
    auto draw_glyph = [&](wchar_t letter) {
        draw_button_glyph(x + 11.0f, cy, letter);
        x += 26.0f;
    };
    auto draw_pill = [&](const wchar_t* text, int len) {
        D2D1_ROUNDED_RECT pill;
        pill.rect = D2D1::RectF(x, cy - 11, x + 28, cy + 11);
        pill.radiusX = 6.0f; pill.radiusY = 6.0f;
        rt_->FillRoundedRectangle(pill, br_text_);
        D2D1_RECT_F tr = pill.rect;
        rt_->DrawText(text, len, text_fmt_, tr, br_center_,
                      D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
        x += 32.0f;
    };

    bool is_submenu = submenu_open_.load(std::memory_order_relaxed);
    int cur = cursor_.load(std::memory_order_relaxed);

    if (name_entry_.load(std::memory_order_relaxed)) {
        draw_glyph(L'A'); draw_text_hint(L"Confirm", 80);
        draw_glyph(L'B'); draw_text_hint(L"Cancel", 70);
        return;
    }
    if (dropdown_open_.load(std::memory_order_relaxed)) {
        const wchar_t* arrows = L"\u25B2\u25BC";
        draw_text_hint(arrows, 30); draw_text_hint(L"Select", 60);
        draw_glyph(L'A'); draw_text_hint(L"Switch", 70);
        draw_glyph(L'B'); draw_text_hint(L"Cancel", 70);
        return;
    }
    if (picker_open_.load(std::memory_order_relaxed)) {
        draw_pill(L"LB", 2); draw_pill(L"RB", 2); draw_text_hint(L"Device", 70);
        draw_glyph(L'A'); draw_text_hint(L"Bind", 50);
        draw_glyph(L'Y'); draw_text_hint(L"None", 50);
        draw_glyph(L'B'); draw_text_hint(L"Back", 50);
        return;
    }
    if (!is_submenu && default_active_.load(std::memory_order_relaxed) && cur < kZoneProfiles) {
        draw_text_hint(L"Default profile is read-only", 240);
        draw_glyph(L'B'); draw_text_hint(L"Close", 60);
        return;
    }

    if (is_submenu) {
        // Submenu action bar depends on selected param type
        ParamType pt = ParamType::Slider;
        {
            std::lock_guard lock(tiles_mtx_);
            if (sub_cursor_ >= 0 && sub_cursor_ < static_cast<int>(sub_params_.size()))
                pt = sub_params_[sub_cursor_].type;
        }

        if (pt == ParamType::Remap) {
            draw_glyph(L'A'); draw_text_hint(L"Choose", 80);
        } else if (pt == ParamType::Slider) {
            // Arrows for adjust
            const wchar_t* arrows = L"\u25C4\u25BA";
            draw_text_hint(arrows, 30); draw_text_hint(L"Adjust", 70);
        } else if (pt == ParamType::Button || pt == ParamType::Toggle) {
            draw_glyph(L'A'); draw_text_hint(L"Change", 80);
        }
        draw_glyph(L'B'); draw_text_hint(L"Back", 50);

    } else if (cur >= kZoneProfiles && cur < kZoneProfiles + kProfileItems) {
        // Profile bar
        int item = cur - kZoneProfiles;
        draw_glyph(L'A');
        draw_text_hint(item == 0 ? L"Open list" : item == 1 ? L"New profile" : L"Delete", 120);
        draw_glyph(L'B'); draw_text_hint(L"Close", 60);

    } else if (cur >= kZoneMain && cur < kZoneMain + kTiles) {
        // Main tiles
        draw_glyph(L'A'); draw_text_hint(L"Change mode", 130);

        // Only show [X] Settings if current mode has submenu params
        bool has_params = false;
        {
            auto profile = config_.get_profile();
            int ti = cur - kZoneMain;
            auto m = static_cast<MainTileId>(ti);
            switch (m) {
                case DPad:
                    has_params = (profile.dpad.mode != config::DpadMode::Dpad);
                    break;
                case LStick: case RStick:
                    has_params = true; // all stick modes have params
                    break;
                case Gyro:
                    has_params = (profile.gyro.mode != config::GyroMode::Disabled);
                    break;
                case LPad: case RPad: {
                    auto& t = (m == LPad) ? profile.left_touchpad : profile.right_touchpad;
                    has_params = (t.mode != config::TouchpadMode::Scroll &&
                                  t.mode != config::TouchpadMode::Disabled);
                    break;
                }
            }
        }
        if (has_params) {
            draw_glyph(L'X'); draw_text_hint(L"Settings", 90);
        }
        draw_glyph(L'B'); draw_text_hint(L"Close", 60);

    } else if (trigger_side_for_cursor(cur) >= 0) {
        // Trigger tiles: both modes have settings.
        draw_glyph(L'A'); draw_text_hint(L"Change mode", 130);
        draw_glyph(L'X'); draw_text_hint(L"Settings", 90);
        draw_glyph(L'B'); draw_text_hint(L"Close", 60);

    } else {
        // Remap tiles: A opens the picker.
        draw_glyph(L'A'); draw_text_hint(L"Remap", 70);
        draw_glyph(L'B'); draw_text_hint(L"Close", 60);
    }
}

void QuickAccessOverlay::draw_description() {
    if (!rt_ || !desc_fmt_ || !br_dim_text_) return;

    std::wstring desc;
    bool is_submenu = submenu_open_.load(std::memory_order_relaxed);

    if (picker_open_.load(std::memory_order_relaxed)) {
        auto dest = static_cast<RemapDest>(picker_dest_.load(std::memory_order_relaxed));
        desc = L"Choose what ";
        if (dest == RemapDest::RadialKey) {
            desc += L"radial sector " + std::to_wstring(picker_sector_.load(std::memory_order_relaxed) + 1);
        } else if (dest == RemapDest::TriggerBinding) {
            int b = std::clamp(picker_sector_.load(std::memory_order_relaxed), 0,
                               static_cast<int>(config::kTriggerBindings) - 1);
            desc += std::wstring(L"binding ") + kBindingLetters[b] + L" of " +
                    trigger_name(picker_side_.load(std::memory_order_relaxed));
        } else if (dest == RemapDest::ClickAction) {
            desc += L"the touchpad click";
        } else {
            int bidv = picker_bid_.load(std::memory_order_relaxed);
            desc += (bidv >= 0) ? button_id_name(static_cast<config::ButtonId>(bidv)) : L"the button";
        }
        desc += L" sends.   LB / RB switch device   -   A binds   -   Y clears   -   B cancels.";
    } else if (is_submenu) {
        std::lock_guard lock(tiles_mtx_);
        if (sub_cursor_ >= 0 && sub_cursor_ < static_cast<int>(sub_descs_.size()))
            desc = sub_descs_[sub_cursor_];
    } else if (name_entry_.load(std::memory_order_relaxed)) {
        desc = L"Type a name for the new profile on the touch keyboard, then Enter or A to confirm.";
    } else if (dropdown_open_.load(std::memory_order_relaxed)) {
        desc = L"Choose a profile to switch to. Default is locked and always available.";
    } else {
        int cur = cursor_.load(std::memory_order_relaxed);
        if (cur >= kZoneProfiles && cur < kZoneProfiles + kProfileItems) {
            int item = cur - kZoneProfiles;
            desc = item == 0 ? L"Switch the active profile. Default is a locked, read-only Xbox mapping."
                 : item == 1 ? L"Create a new editable profile from the standard Default mapping."
                             : L"Delete the selected profile. The Default profile cannot be deleted.";
        } else if (default_active_.load(std::memory_order_relaxed)) {
            desc = L"Default is a read-only standard Xbox mapping. Press New to make an editable copy.";
        } else if (cur >= kZoneMain && cur < kZoneMain + kTiles) {
            desc = kMainDescriptions[cur - kZoneMain];
        } else if (int side = trigger_side_for_cursor(cur); side >= 0) {
            desc = std::wstring(side == 0 ? L"Left" : L"Right") +
                   L" trigger. A switches between the analog Xbox trigger and Buttons "
                   L"(bindings A, B and C at their own pull depths). X opens its settings.";
        } else {
            desc = L"Press A to choose a new binding for this button.";
        }
    }

    float right_edge = static_cast<float>(is_submenu ? kTotalW + kSubmenuW : kTotalW);
    float y = menu_top() + static_cast<float>(kMenuH + kDescGap);

    // Background
    D2D1_ROUNDED_RECT desc_bg;
    desc_bg.rect = D2D1::RectF(2.0f, y - 4.0f, right_edge - 2.0f, y + kDescAreaH + 4.0f);
    desc_bg.radiusX = 6.0f; desc_bg.radiusY = 6.0f;
    rt_->FillRoundedRectangle(desc_bg, br_center_);

    D2D1_RECT_F dr = D2D1::RectF(
        static_cast<float>(kPad) + 4.0f, y,
        right_edge - static_cast<float>(kPad) - 4.0f, y + kDescAreaH);
    rt_->DrawText(desc.c_str(), static_cast<UINT32>(desc.size()),
                  desc_fmt_, dr, br_dim_text_,
                  D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
}

void QuickAccessOverlay::draw_profile_bar(float y) {
    if (!rt_ || !action_fmt_ || !mini_fmt_) return;

    if (name_entry_.load(std::memory_order_relaxed)) {
        draw_name_entry(y);
        return;
    }

    int cur = cursor_.load(std::memory_order_relaxed);
    const float top = y;
    const float h = static_cast<float>(kProfileRowH);
    const float left = static_cast<float>(kPad);
    const float right = static_cast<float>(kTotalW - kPad);
    const float label_w = 62.0f;
    const float btn_w = 94.0f;
    const float btn_gap = 8.0f;

    // "Profile" label
    D2D1_RECT_F lblr = D2D1::RectF(left, top, left + label_w, top + h);
    rt_->DrawText(L"Profile", 7, action_fmt_, lblr, br_dim_text_,
                  D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);

    float del_x = right - btn_w;
    float new_x = del_x - btn_gap - btn_w;
    bool active_default = default_active_.load(std::memory_order_relaxed);

    auto draw_button = [&](float bx, const wchar_t* text, bool focused, bool enabled) {
        D2D1_ROUNDED_RECT r;
        r.rect = D2D1::RectF(bx, top, bx + btn_w, top + h);
        r.radiusX = 6.0f; r.radiusY = 6.0f;
        rt_->FillRoundedRectangle(r, focused ? br_hover_ : br_sector_);
        // White like the other tiles, orange when selected (matches submenu selection).
        ID2D1SolidColorBrush* txt = !enabled ? br_dim_text_ : (focused ? br_active_ : br_text_);
        rt_->DrawText(text, static_cast<UINT32>(wcslen(text)), mini_fmt_, r.rect,
                      txt, D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
    };

    // Dropdown box (fills the middle)
    float dd_left = left + label_w + 8.0f;
    float dd_right = new_x - 12.0f;
    std::wstring active_name = (dd_active_ >= 0 && dd_active_ < static_cast<int>(dd_names_.size()))
                                   ? dd_names_[dd_active_] : L"Default";
    {
        D2D1_ROUNDED_RECT r;
        r.rect = D2D1::RectF(dd_left, top, dd_right, top + h);
        r.radiusX = 6.0f; r.radiusY = 6.0f;
        bool focused = (cur == kZoneProfiles);
        rt_->FillRoundedRectangle(r, focused ? br_hover_ : br_slider_bg_);

        D2D1_RECT_F tr = D2D1::RectF(dd_left + 12.0f, top, dd_right - 26.0f, top + h);
        rt_->DrawText(active_name.c_str(), static_cast<UINT32>(active_name.size()),
                      action_fmt_, tr, focused ? br_active_ : br_text_,
                      D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);

        const wchar_t* chev = dropdown_open_.load(std::memory_order_relaxed) ? L"\u25B4" : L"\u25BE";
        D2D1_RECT_F cr = D2D1::RectF(dd_right - 24.0f, top, dd_right - 6.0f, top + h);
        rt_->DrawText(chev, 1, mini_fmt_, cr, br_dim_text_,
                      D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
    }

    draw_button(new_x, L"New", cur == kZoneProfiles + 1, true);
    draw_button(del_x, L"Delete", cur == kZoneProfiles + 2, !active_default);

    // Open dropdown list (downward over the tiles)
    if (dropdown_open_.load(std::memory_order_relaxed) && !dd_names_.empty()) {
        int n = static_cast<int>(dd_names_.size());
        const float row_h = 30.0f;
        float list_top = top + h + 4.0f;
        float list_bottom = list_top + n * row_h + 8.0f;

        D2D1_ROUNDED_RECT box;
        box.rect = D2D1::RectF(dd_left, list_top, dd_right, list_bottom);
        box.radiusX = 6.0f; box.radiusY = 6.0f;
        rt_->FillRoundedRectangle(box, br_center_);
        rt_->DrawRoundedRectangle(box, br_dim_text_, 1.0f);

        for (int i = 0; i < n; i++) {
            float ry = list_top + 4.0f + i * row_h;
            if (i == dd_cursor_) {
                D2D1_ROUNDED_RECT hr;
                hr.rect = D2D1::RectF(dd_left + 4.0f, ry, dd_right - 4.0f, ry + row_h);
                hr.radiusX = 4.0f; hr.radiusY = 4.0f;
                rt_->FillRoundedRectangle(hr, br_hover_);
            }
            bool is_def = (dd_names_[i] == L"Default");
            bool is_act = (i == dd_active_);
            D2D1_RECT_F tr = D2D1::RectF(dd_left + 12.0f, ry, dd_right - 62.0f, ry + row_h);
            rt_->DrawText(dd_names_[i].c_str(), static_cast<UINT32>(dd_names_[i].size()),
                          action_fmt_, tr, is_act ? br_active_ : br_text_,
                          D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
            const wchar_t* tag = is_def ? L"locked" : (is_act ? L"active" : L"");
            if (tag[0]) {
                D2D1_RECT_F gr = D2D1::RectF(dd_right - 58.0f, ry, dd_right - 10.0f, ry + row_h);
                rt_->DrawText(tag, static_cast<UINT32>(wcslen(tag)), action_fmt_, gr, br_dim_text_,
                              D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
            }
        }
    }
}

void QuickAccessOverlay::draw_name_entry(float y) {
    const float top = y;
    const float h = static_cast<float>(kProfileRowH);
    const float left = static_cast<float>(kPad);
    const float right = static_cast<float>(kTotalW - kPad);
    const float label_w = 92.0f;

    D2D1_RECT_F lblr = D2D1::RectF(left, top, left + label_w, top + h);
    rt_->DrawText(L"New name", 8, action_fmt_, lblr, br_dim_text_,
                  D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);

    float fx = left + label_w + 8.0f;
    D2D1_ROUNDED_RECT field;
    field.rect = D2D1::RectF(fx, top, right, top + h);
    field.radiusX = 6.0f; field.radiusY = 6.0f;
    rt_->FillRoundedRectangle(field, br_slider_bg_);

    std::wstring shown = name_queue_.text();
    shown += L"|";  // caret
    D2D1_RECT_F tr = D2D1::RectF(fx + 12.0f, top, right - 12.0f, top + h);
    rt_->DrawText(shown.c_str(), static_cast<UINT32>(shown.size()), action_fmt_, tr, br_text_,
                  D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
}

void QuickAccessOverlay::draw_submenu() {
    if (!rt_ || !mode_fmt_ || !label_fmt_ || !br_dim_overlay_) return;

    float my = menu_top();

    // Dim the main menu
    rt_->FillRectangle(D2D1::RectF(0, my, static_cast<float>(kTotalW), my + kMenuH), br_dim_overlay_);

    // Submenu panel background
    float sx = static_cast<float>(kTotalW);
    float sw = static_cast<float>(kSubmenuW);
    D2D1_ROUNDED_RECT bg;
    bg.rect = D2D1::RectF(sx, my, sx + sw, my + kMenuH);
    bg.radiusX = 0.0f; bg.radiusY = 0.0f;
    // Right side rounded corners
    rt_->FillRectangle(bg.rect, br_center_);
    // Rounded right edge
    D2D1_ROUNDED_RECT rbg;
    rbg.rect = D2D1::RectF(sx + sw - 8, my, sx + sw, my + kMenuH);
    rbg.radiusX = 6.0f; rbg.radiusY = 6.0f;
    rt_->FillRoundedRectangle(rbg, br_center_);

    // Header: "TileName > ModeName"
    // Called under tiles_mtx_ from on_paint
    std::wstring header;
    if (submenu_trigger_)
        header = sub_header_;
    else if (submenu_tile_ >= 0 && submenu_tile_ < kTiles)
        header = tiles_[submenu_tile_].label + L" > " + tiles_[submenu_tile_].mode_text;

    float hx = sx + kPad;
    float hy = my + kPad;
    D2D1_RECT_F hr = D2D1::RectF(hx, hy, sx + sw - kPad, hy + 30);
    rt_->DrawText(header.c_str(), static_cast<UINT32>(header.size()),
                  mode_fmt_, hr, br_text_,
                  D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);

    // Parameters list
    float py = hy + 40;
    for (int i = 0; i < static_cast<int>(sub_params_.size()); i++) {
        const auto& p = sub_params_[i];
        bool selected = (i == sub_cursor_);
        float pw = sw - 2 * kPad;

        // Label (left-aligned)
        ID2D1SolidColorBrush* label_br = selected ? br_active_ : br_text_;
        D2D1_RECT_F lr = D2D1::RectF(hx, py + 4, hx + pw * 0.55f, py + 38);
        rt_->DrawText(p.label.c_str(), static_cast<UINT32>(p.label.size()),
                      action_fmt_, lr, label_br,
                      D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);

        // Value
        float vx = hx + pw * 0.55f;
        float vw = pw * 0.45f;

        if (p.type == ParamType::Slider) {
            // Slider bar + numeric
            float bar_y = py + 17;
            float bar_h = 8.0f;
            float bar_w = vw - 60;
            D2D1_ROUNDED_RECT bar_bg;
            bar_bg.rect = D2D1::RectF(vx, bar_y, vx + bar_w, bar_y + bar_h);
            bar_bg.radiusX = 4.0f; bar_bg.radiusY = 4.0f;
            rt_->FillRoundedRectangle(bar_bg, br_slider_bg_);

            float norm = (p.max_val > p.min_val) ? (p.value - p.min_val) / (p.max_val - p.min_val) : 0;
            float fill_w = bar_w * std::clamp(norm, 0.0f, 1.0f);
            if (fill_w > 0.5f) {
                D2D1_ROUNDED_RECT bar_fill;
                bar_fill.rect = D2D1::RectF(vx, bar_y, vx + fill_w, bar_y + bar_h);
                bar_fill.radiusX = 4.0f; bar_fill.radiusY = 4.0f;
                rt_->FillRoundedRectangle(bar_fill, selected ? br_active_ : br_hover_);
            }

            D2D1_RECT_F vr = D2D1::RectF(vx + bar_w + 8, py + 4, hx + pw, py + 38);
            rt_->DrawText(p.value_text.c_str(), static_cast<UINT32>(p.value_text.size()),
                          mode_fmt_, vr, br_text_,
                          D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
        } else {
            // Button/Toggle/Remap: just value text
            D2D1_RECT_F vr = D2D1::RectF(vx, py + 4, hx + pw, py + 38);
            rt_->DrawText(p.value_text.c_str(), static_cast<UINT32>(p.value_text.size()),
                          mode_fmt_, vr, selected ? br_active_ : br_text_,
                          D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
        }

        py += 42;
    }
}

// --- Remap picker ---

void QuickAccessOverlay::build_picker_targets() {
    using config::ActionType;

    const float u = 32.0f, gap = 4.0f, keyH = 32.0f, rowGap = 4.0f;
    const float my = static_cast<float>(kActionBarH + kActionBarGap);   // top of menu panel
    const float contentTop = my + static_cast<float>(kPad) + kPickerTabH + 16.0f;

    // ---------- Tab 1: Keyboard (tenkeyless) ----------
    struct K { const wchar_t* l; uint16_t vk; float w; };
    const std::vector<std::vector<K>> main = {
        {{L"Esc",VK_ESCAPE,1},{L"F1",VK_F1,1},{L"F2",VK_F2,1},{L"F3",VK_F3,1},{L"F4",VK_F4,1},
         {L"F5",VK_F5,1},{L"F6",VK_F6,1},{L"F7",VK_F7,1},{L"F8",VK_F8,1},
         {L"F9",VK_F9,1},{L"F10",VK_F10,1},{L"F11",VK_F11,1},{L"F12",VK_F12,1},
         {L"PrtSc",VK_SNAPSHOT,1},{L"ScrLk",VK_SCROLL,1},{L"Pause",VK_PAUSE,1}},
        {{L"`",VK_OEM_3,1},{L"1",'1',1},{L"2",'2',1},{L"3",'3',1},{L"4",'4',1},{L"5",'5',1},
         {L"6",'6',1},{L"7",'7',1},{L"8",'8',1},{L"9",'9',1},{L"0",'0',1},
         {L"-",VK_OEM_MINUS,1},{L"=",VK_OEM_PLUS,1},{L"Bksp",VK_BACK,2}},
        {{L"Tab",VK_TAB,1.5f},{L"Q",'Q',1},{L"W",'W',1},{L"E",'E',1},{L"R",'R',1},{L"T",'T',1},
         {L"Y",'Y',1},{L"U",'U',1},{L"I",'I',1},{L"O",'O',1},{L"P",'P',1},
         {L"[",VK_OEM_4,1},{L"]",VK_OEM_6,1},{L"\\",VK_OEM_5,1.5f}},
        {{L"Caps",VK_CAPITAL,1.75f},{L"A",'A',1},{L"S",'S',1},{L"D",'D',1},{L"F",'F',1},{L"G",'G',1},
         {L"H",'H',1},{L"J",'J',1},{L"K",'K',1},{L"L",'L',1},{L";",VK_OEM_1,1},{L"'",VK_OEM_7,1},
         {L"Enter",VK_RETURN,2.25f}},
        {{L"Shift",VK_LSHIFT,2.25f},{L"Z",'Z',1},{L"X",'X',1},{L"C",'C',1},{L"V",'V',1},{L"B",'B',1},
         {L"N",'N',1},{L"M",'M',1},{L",",VK_OEM_COMMA,1},{L".",VK_OEM_PERIOD,1},{L"/",VK_OEM_2,1},
         {L"Shift",VK_RSHIFT,2.75f}},
        {{L"Ctrl",VK_LCONTROL,1.25f},{L"Win",VK_LWIN,1.25f},{L"Alt",VK_LMENU,1.25f},
         {L"Space",VK_SPACE,6.25f},{L"Alt",VK_RMENU,1.25f},{L"Win",VK_RWIN,1.25f},
         {L"Menu",VK_APPS,1.25f},{L"Ctrl",VK_RCONTROL,1.25f}},
    };

    auto rowWidth = [&](const std::vector<K>& row) {
        float w = 0;
        for (const auto& k : row) w += k.w * u;
        w += static_cast<float>(row.size() - 1) * gap;
        return w;
    };
    float mainW = 0;
    for (const auto& r : main) mainW = (std::max)(mainW, rowWidth(r));

    const float navKeyW = 50.0f;
    const float navBlockW = 3 * navKeyW + 2 * gap;
    const float blockGap = 20.0f;
    const float totalKbW = mainW + blockGap + navBlockW;
    const float kbX = (static_cast<float>(kTotalW) - totalKbW) / 2.0f;
    const float navX = kbX + mainW + blockGap;

    std::vector<PickerTarget> kb;
    for (size_t r = 0; r < main.size(); r++) {
        float rx = kbX;
        float ry = contentTop + static_cast<float>(r) * (keyH + rowGap);
        for (const auto& k : main[r]) {
            float kw = k.w * u;
            PickerTarget t;
            t.cx = rx + kw / 2.0f; t.cy = ry + keyH / 2.0f;
            t.w = kw; t.h = keyH; t.label = k.l;
            t.action = { ActionType::Keyboard, k.vk };
            kb.push_back(std::move(t));
            rx += kw + gap;
        }
    }
    // Navigation editing cluster aligned to the first two keyboard rows
    struct N { const wchar_t* l; uint16_t vk; };
    const N navEdit[2][3] = {
        {{L"Ins",VK_INSERT},{L"Home",VK_HOME},{L"PgUp",VK_PRIOR}},
        {{L"Del",VK_DELETE},{L"End",VK_END},{L"PgDn",VK_NEXT}},
    };
    for (int r = 0; r < 2; r++) {
        float ry = contentTop + static_cast<float>(r) * (keyH + rowGap);
        for (int c = 0; c < 3; c++) {
            float rx = navX + c * (navKeyW + gap);
            PickerTarget t;
            t.cx = rx + navKeyW / 2.0f; t.cy = ry + keyH / 2.0f;
            t.w = navKeyW; t.h = keyH; t.label = navEdit[r][c].l;
            t.action = { ActionType::Keyboard, navEdit[r][c].vk };
            kb.push_back(std::move(t));
        }
    }
    // Arrows (inverted-T) below the editing cluster. Labels built from code
    // points so the source stays ASCII regardless of compiler encoding flags.
    float arrowsTop = contentTop + 2 * (keyH + rowGap) + 12.0f;
    auto addArrow = [&](wchar_t glyph, uint16_t vk, float col, float rowY) {
        float rx = navX + col * (navKeyW + gap);
        PickerTarget t;
        t.cx = rx + navKeyW / 2.0f; t.cy = rowY + keyH / 2.0f;
        t.w = navKeyW; t.h = keyH; t.label = std::wstring(1, glyph);
        t.action = { ActionType::Keyboard, vk };
        kb.push_back(std::move(t));
    };
    addArrow(static_cast<wchar_t>(0x2191), VK_UP, 1, arrowsTop);           // up
    float arrowsRow2 = arrowsTop + keyH + rowGap;
    addArrow(static_cast<wchar_t>(0x2190), VK_LEFT, 0, arrowsRow2);
    addArrow(static_cast<wchar_t>(0x2193), VK_DOWN, 1, arrowsRow2);
    addArrow(static_cast<wchar_t>(0x2192), VK_RIGHT, 2, arrowsRow2);
    picker_targets_[1] = std::move(kb);

    // ---------- Round-button tabs (Mouse=0, Xbox=2) ----------
    const float boardX = (static_cast<float>(kTotalW) - 748.0f) / 2.0f;
    const float boardY = contentTop;
    const float R = 29.0f;
    auto addRound = [&](std::vector<PickerTarget>& v, std::wstring l,
                        float bx, float by, ActionType type, uint16_t code) {
        PickerTarget t;
        t.cx = boardX + bx; t.cy = boardY + by; t.w = 2 * R; t.h = 2 * R;
        t.label = std::move(l); t.action = { type, code };
        v.push_back(std::move(t));
    };

    std::vector<PickerTarget> mouse;
    addRound(mouse, L"L", 300, 110, ActionType::MouseButton, 0);
    addRound(mouse, L"R", 448, 110, ActionType::MouseButton, 1);
    addRound(mouse, L"M", 374, 145, ActionType::MouseButton, 2);
    addRound(mouse, std::wstring(1, static_cast<wchar_t>(0x2191)), 374, 80,  ActionType::MouseScroll, 1);
    addRound(mouse, std::wstring(1, static_cast<wchar_t>(0x2193)), 374, 210, ActionType::MouseScroll, 0);
    picker_targets_[0] = std::move(mouse);

    std::vector<PickerTarget> pad;
    addRound(pad, L"A",     600, 200, ActionType::XboxButton, 0x1000);
    addRound(pad, L"B",     645, 155, ActionType::XboxButton, 0x2000);
    addRound(pad, L"X",     555, 155, ActionType::XboxButton, 0x4000);
    addRound(pad, L"Y",     600, 110, ActionType::XboxButton, 0x8000);
    addRound(pad, L"LB",    224, 50,  ActionType::XboxButton, 0x0100);
    addRound(pad, L"RB",    524, 50,  ActionType::XboxButton, 0x0200);
    addRound(pad, L"Start", 334, 130, ActionType::XboxButton, 0x0010);
    addRound(pad, L"Back",  414, 130, ActionType::XboxButton, 0x0020);
    addRound(pad, L"LS",    324, 225, ActionType::XboxButton, 0x0040);
    addRound(pad, L"RS",    424, 225, ActionType::XboxButton, 0x0080);
    picker_targets_[2] = std::move(pad);
}

void QuickAccessOverlay::open_picker() {
    int cur = cursor_.load(std::memory_order_relaxed);
    auto bid = button_id_for_cursor(cur);
    if (bid == config::ButtonId::Count) return;

    auto profile = config_.get_profile();
    picker_dest_.store(static_cast<int>(RemapDest::Button), std::memory_order_relaxed);
    picker_bid_.store(static_cast<int>(bid), std::memory_order_relaxed);
    picker_from_submenu_.store(false, std::memory_order_relaxed);
    open_picker_for(profile.buttons[static_cast<int>(bid)]);
}

void QuickAccessOverlay::open_picker_submenu(RemapDest dest, int target, int index) {
    auto profile = config_.get_profile();
    config::ButtonAction current{config::ActionType::None, 0};
    if (dest == RemapDest::TriggerBinding) {
        if (index < 0 || index >= static_cast<int>(config::kTriggerBindings)) return;
        const auto& t = (target == 0) ? profile.left_trigger : profile.right_trigger;
        current = t.bindings[static_cast<size_t>(index)].action;
        picker_side_.store(target, std::memory_order_relaxed);
    } else {
        auto ti = static_cast<MainTileId>(target);
        const auto& t = (ti == LPad) ? profile.left_touchpad : profile.right_touchpad;
        current = (dest == RemapDest::ClickAction) ? t.click_action : t.radial_keys[index];
        picker_pad_.store(target, std::memory_order_relaxed);
    }

    picker_dest_.store(static_cast<int>(dest), std::memory_order_relaxed);
    picker_sector_.store(index, std::memory_order_relaxed);
    picker_from_submenu_.store(true, std::memory_order_relaxed);

    // Collapse the submenu panel so the picker renders centered at the base width;
    // close_picker() reopens it. Input handlers check picker_open_ before submenu_open_,
    // so the transient submenu_open_=false does not affect navigation.
    submenu_open_.store(false, std::memory_order_relaxed);
    PostMessage(hwnd(), WM_OVR_RESIZE, kTotalW, 0);

    open_picker_for(current);
}

void QuickAccessOverlay::open_picker_for(const config::ButtonAction& action) {
    int tab = 1;  // default landing on the keyboard
    switch (action.type) {
        case config::ActionType::MouseButton:
        case config::ActionType::MouseScroll: tab = 0; break;
        case config::ActionType::Keyboard:    tab = 1; break;
        case config::ActionType::XboxButton:  tab = 2; break;
        case config::ActionType::None:        tab = 2; break;
    }

    int idx = 0, boundTab = -1, boundIdx = -1;
    const auto& ts = picker_targets_[tab];
    for (int i = 0; i < static_cast<int>(ts.size()); i++) {
        if (ts[i].action.type == action.type && ts[i].action.code == action.code) {
            idx = i; boundTab = tab; boundIdx = i; break;
        }
    }

    picker_tab_.store(tab, std::memory_order_relaxed);
    picker_cursor_[tab].store(idx, std::memory_order_relaxed);
    picker_bound_tab_.store(boundTab, std::memory_order_relaxed);
    picker_bound_idx_.store(boundIdx, std::memory_order_relaxed);
    picker_open_.store(true, std::memory_order_relaxed);
    invalidate();
}

void QuickAccessOverlay::picker_navigate(int dx, int dy) {
    int tab = picker_tab_.load(std::memory_order_relaxed);
    const auto& ts = picker_targets_[tab];
    if (ts.empty()) return;
    int cur = picker_cursor_[tab].load(std::memory_order_relaxed);
    if (cur < 0 || cur >= static_cast<int>(ts.size())) cur = 0;
    const auto& c = ts[cur];

    // Directional (spatial) focus: pick the nearest target in the pressed
    // direction, penalizing cross-axis offset so aligned keys win.
    int best = -1;
    float bestScore = 1e30f;
    for (int i = 0; i < static_cast<int>(ts.size()); i++) {
        if (i == cur) continue;
        float ddx = ts[i].cx - c.cx;
        float ddy = ts[i].cy - c.cy;
        float primary, cross;
        if (dx > 0)      { if (ddx <= 1.0f) continue;  primary = ddx;  cross = fabsf(ddy); }
        else if (dx < 0) { if (ddx >= -1.0f) continue; primary = -ddx; cross = fabsf(ddy); }
        else if (dy > 0) { if (ddy <= 1.0f) continue;  primary = ddy;  cross = fabsf(ddx); }
        else             { if (ddy >= -1.0f) continue; primary = -ddy; cross = fabsf(ddx); }
        float score = primary + cross * 2.0f;
        if (score < bestScore) { bestScore = score; best = i; }
    }
    if (best >= 0) {
        picker_cursor_[tab].store(best, std::memory_order_relaxed);
        invalidate();
    }
}

void QuickAccessOverlay::picker_switch_tab(int direction) {
    int t = (picker_tab_.load(std::memory_order_relaxed) + direction + 3) % 3;
    picker_tab_.store(t, std::memory_order_relaxed);
    invalidate();
}

// Write an action to whatever destination the picker is currently bound to.
void QuickAccessOverlay::picker_write(const config::ButtonAction& a) {
    auto dest = static_cast<RemapDest>(picker_dest_.load(std::memory_order_relaxed));
    auto profile = config_.get_profile();
    if (dest == RemapDest::Button) {
        int bid = picker_bid_.load(std::memory_order_relaxed);
        if (bid < 0) return;
        profile.buttons[bid] = a;
    } else if (dest == RemapDest::TriggerBinding) {
        int b = picker_sector_.load(std::memory_order_relaxed);
        if (b < 0 || b >= static_cast<int>(config::kTriggerBindings)) return;
        auto& t = (picker_side_.load(std::memory_order_relaxed) == 0) ? profile.left_trigger
                                                                       : profile.right_trigger;
        t.bindings[static_cast<size_t>(b)].action = a;
    } else {
        auto ti = static_cast<MainTileId>(picker_pad_.load(std::memory_order_relaxed));
        auto& t = (ti == LPad) ? profile.left_touchpad : profile.right_touchpad;
        if (dest == RemapDest::RadialKey) {
            int s = picker_sector_.load(std::memory_order_relaxed);
            if (s < 0 || s >= static_cast<int>(t.radial_keys.size())) return;
            t.radial_keys[s] = a;
        } else {
            t.click_action = a;
        }
    }
    config_.set_profile(profile);
    mapper_.apply_profile(profile, synth_);
    refresh_all(profile);
}

void QuickAccessOverlay::picker_bind_current() {
    int tab = picker_tab_.load(std::memory_order_relaxed);
    const auto& ts = picker_targets_[tab];
    int cur = picker_cursor_[tab].load(std::memory_order_relaxed);
    if (cur >= 0 && cur < static_cast<int>(ts.size()))
        picker_write(ts[cur].action);
    close_picker();
}

void QuickAccessOverlay::picker_set_none() {
    picker_write({ config::ActionType::None, 0 });
    close_picker();
}

void QuickAccessOverlay::press_y() {
    if (!picker_open_.load(std::memory_order_relaxed)) return;
    picker_set_none();
}

void QuickAccessOverlay::close_picker() {
    picker_open_.store(false, std::memory_order_relaxed);
    if (picker_from_submenu_.exchange(false, std::memory_order_relaxed)) {
        // Rebuild the params so the new binding's value text shows, then reopen the
        // submenu panel (widen the window back to the submenu width).
        auto profile = config_.get_profile();
        {
            std::lock_guard lock(tiles_mtx_);
            rebuild_submenu(profile);   // the main tile's or the trigger's, cursor kept
        }
        submenu_open_.store(true, std::memory_order_relaxed);
        PostMessage(hwnd(), WM_OVR_RESIZE, kTotalW + kSubmenuW, 0);
    }
    invalidate();
}

void QuickAccessOverlay::draw_picker_tabs(float x, float y, float w) {
    if (!rt_ || !mini_fmt_) return;
    int active = picker_tab_.load(std::memory_order_relaxed);
    const float gap = 10.0f;
    float tw = (w - 2 * gap) / 3.0f;
    for (int i = 0; i < 3; i++) {
        float tx = x + i * (tw + gap);
        D2D1_ROUNDED_RECT r;
        r.rect = D2D1::RectF(tx, y, tx + tw, y + kPickerTabH);
        r.radiusX = 6.0f; r.radiusY = 6.0f;
        bool sel = (i == active);
        rt_->FillRoundedRectangle(r, sel ? br_hover_ : br_sector_);
        if (sel) {
            rt_->FillRectangle(
                D2D1::RectF(tx + 6, y + kPickerTabH - 3, tx + tw - 6, y + kPickerTabH - 1),
                br_active_);
        }
        rt_->DrawText(kPickerTabLabels[i], static_cast<UINT32>(wcslen(kPickerTabLabels[i])),
                      mini_fmt_, r.rect, sel ? br_text_ : br_dim_text_,
                      D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
    }
}

void QuickAccessOverlay::draw_picker() {
    if (!rt_) return;
    float my = menu_top();

    // Tab bar
    float tabsX = static_cast<float>(kPad);
    float tabsY = my + static_cast<float>(kPad);
    float tabsW = static_cast<float>(kTotalW - 2 * kPad);
    draw_picker_tabs(tabsX, tabsY, tabsW);

    int tab  = picker_tab_.load(std::memory_order_relaxed);
    int cur  = picker_cursor_[tab].load(std::memory_order_relaxed);
    int btab = picker_bound_tab_.load(std::memory_order_relaxed);
    int bidx = picker_bound_idx_.load(std::memory_order_relaxed);
    const auto& ts = picker_targets_[tab];
    bool round = (tab != 1);

    for (int i = 0; i < static_cast<int>(ts.size()); i++) {
        const auto& t = ts[i];
        bool selected = (i == cur);
        bool bound = (btab == tab && bidx == i);
        D2D1_RECT_F rect = D2D1::RectF(t.cx - t.w / 2, t.cy - t.h / 2, t.cx + t.w / 2, t.cy + t.h / 2);
        ID2D1SolidColorBrush* fill = selected ? br_hover_ : br_sector_;
        ID2D1SolidColorBrush* txt  = bound ? br_active_ : br_text_;

        if (round) {
            D2D1_ELLIPSE e = D2D1::Ellipse(D2D1::Point2F(t.cx, t.cy), t.w / 2, t.h / 2);
            rt_->FillEllipse(e, fill);
            if (selected)   rt_->DrawEllipse(e, br_active_, 2.5f);
            else if (bound) rt_->DrawEllipse(e, br_active_, 1.5f);
            rt_->DrawText(t.label.c_str(), static_cast<UINT32>(t.label.size()),
                          mini_fmt_, rect, txt, D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
        } else {
            D2D1_ROUNDED_RECT rr; rr.rect = rect; rr.radiusX = 5.0f; rr.radiusY = 5.0f;
            rt_->FillRoundedRectangle(rr, fill);
            if (selected)   rt_->DrawRoundedRectangle(rr, br_active_, 2.0f);
            else if (bound) rt_->DrawRoundedRectangle(rr, br_active_, 1.5f);
            rt_->DrawText(t.label.c_str(), static_cast<UINT32>(t.label.size()),
                          key_fmt_, rect, txt, D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
        }
    }
}

} // namespace windeck::overlay
