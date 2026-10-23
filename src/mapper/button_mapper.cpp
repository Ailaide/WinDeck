#include "mapper/button_mapper.h"
#include "neptune/neptune_report.h"

#ifndef XUSB_GAMEPAD_DPAD_UP
#define XUSB_GAMEPAD_DPAD_UP        0x0001
#define XUSB_GAMEPAD_DPAD_DOWN      0x0002
#define XUSB_GAMEPAD_DPAD_LEFT      0x0004
#define XUSB_GAMEPAD_DPAD_RIGHT     0x0008
#define XUSB_GAMEPAD_START          0x0010
#define XUSB_GAMEPAD_BACK           0x0020
#define XUSB_GAMEPAD_LEFT_THUMB     0x0040
#define XUSB_GAMEPAD_RIGHT_THUMB    0x0080
#define XUSB_GAMEPAD_LEFT_SHOULDER  0x0100
#define XUSB_GAMEPAD_RIGHT_SHOULDER 0x0200
#define XUSB_GAMEPAD_GUIDE          0x0400
#define XUSB_GAMEPAD_A              0x1000
#define XUSB_GAMEPAD_B              0x2000
#define XUSB_GAMEPAD_X              0x4000
#define XUSB_GAMEPAD_Y              0x8000
#endif

namespace windeck::mapper {

using namespace windeck::neptune;
using config::ActionType;
using config::ButtonId;

void ButtonMapper::set_config(
    const std::array<config::ButtonAction, static_cast<size_t>(ButtonId::Count)>& actions) {
    actions_ = actions;
    configured_ = true;
}

USHORT ButtonMapper::map(uint32_t nb, uint32_t nb_ext,
                          SystemButtonEvents& sys_events,
                          windeck::output::SyntheticInput& synth) {
    USHORT xb = 0;

    // --- System buttons: intercept, do NOT pass to Xbox ---
    // On first frame, suppress rising edges to avoid false triggers
    // (prev_buttons_ is 0, so any held button looks like a new press)
    uint32_t rising_btn = first_frame_ ? 0u : (nb & ~prev_buttons_);
    uint32_t rising_ext = first_frame_ ? 0u : (nb_ext & ~prev_buttons_ext_);
    first_frame_ = false;
    prev_buttons_ = nb;
    prev_buttons_ext_ = nb_ext;

    sys_events.steam_pressed        = (rising_btn & btn::kSteam) != 0;
    sys_events.quick_access_pressed = (rising_ext & btn_ext::kQuickAccess) != 0;

    // D-Pad is handled exclusively by DpadMapper (avoids double mapping)

    // --- Remappable buttons ---
    if (!configured_) {
        // Fallback: hardcoded 1:1 Xbox mapping (pre-config)
        if (nb & btn::kA)           xb |= XUSB_GAMEPAD_A;
        if (nb & btn::kB)           xb |= XUSB_GAMEPAD_B;
        if (nb & btn::kX)           xb |= XUSB_GAMEPAD_X;
        if (nb & btn::kY)           xb |= XUSB_GAMEPAD_Y;
        if (nb & btn::kLShoulder)   xb |= XUSB_GAMEPAD_LEFT_SHOULDER;
        if (nb & btn::kRShoulder)   xb |= XUSB_GAMEPAD_RIGHT_SHOULDER;
        if (nb & btn::kOptions)     xb |= XUSB_GAMEPAD_START;
        if (nb & btn::kMenu)        xb |= XUSB_GAMEPAD_BACK;
        if (nb & btn::kLStickClick) xb |= XUSB_GAMEPAD_LEFT_THUMB;
        if (nb & btn::kRStickClick) xb |= XUSB_GAMEPAD_RIGHT_THUMB;
        return xb;
    }

    // Every held keyboard or mouse-button binding goes into one set, replaced
    // each report. SyntheticInput applies the union with the other sources, so
    // a key stays pressed as long as ANY button, stick or pad still holds it.
    windeck::output::HoldSet holds;

    for (size_t i = 0; i < static_cast<size_t>(ButtonId::Count); i++) {
        const auto& nmap = kNeptuneMap[i];
        bool pressed = false;
        if (nmap.btn_mask) pressed |= (nb & nmap.btn_mask) != 0;
        if (nmap.ext_mask) pressed |= (nb_ext & nmap.ext_mask) != 0;

        // Rising edge: true only on the frame the button was just pressed
        bool rising = false;
        if (nmap.btn_mask) rising |= (rising_btn & nmap.btn_mask) != 0;
        if (nmap.ext_mask) rising |= (rising_ext & nmap.ext_mask) != 0;

        const auto& action = actions_[i];
        switch (action.type) {
            case ActionType::XboxButton:
                if (pressed && action.code != 0)
                    xb |= static_cast<USHORT>(action.code);
                break;
            case ActionType::Keyboard:
            case ActionType::MouseButton:
                if (pressed) holds.add(action);
                break;
            case ActionType::MouseScroll:
                // One tick per press (rising edge only), code=1 → up, code=0 → down
                if (rising)
                    synth.mouse_scroll(action.code ? WHEEL_DELTA : -WHEEL_DELTA);
                break;
            case ActionType::None:
                break;
        }
    }

    synth.set_holds(windeck::output::HoldSource::Buttons, holds);
    return xb;
}

void ButtonMapper::release_all(windeck::output::SyntheticInput& synth) {
    synth.clear_holds(windeck::output::HoldSource::Buttons);
}

} // namespace windeck::mapper
