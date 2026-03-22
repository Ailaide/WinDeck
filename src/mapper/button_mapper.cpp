#include "mapper/button_mapper.h"
#include "neptune/neptune_report.h"

// Xbox 360 button constants from XUSB_REPORT
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

USHORT ButtonMapper::map(uint32_t nb, uint32_t nb_ext,
                          SystemButtonEvents& sys_events) {
    USHORT xb = 0;

    // --- System buttons: intercept, do NOT pass to Xbox ---
    uint32_t rising_btn = nb & ~prev_buttons_;
    uint32_t rising_ext = nb_ext & ~prev_buttons_ext_;
    prev_buttons_ = nb;
    prev_buttons_ext_ = nb_ext;

    sys_events.steam_pressed        = (rising_btn & btn::kSteam) != 0;
    sys_events.quick_access_pressed = (rising_ext & btn_ext::kQuickAccess) != 0;

    // --- Face buttons ---
    if (nb & btn::kA)           xb |= XUSB_GAMEPAD_A;
    if (nb & btn::kB)           xb |= XUSB_GAMEPAD_B;
    if (nb & btn::kX)           xb |= XUSB_GAMEPAD_X;
    if (nb & btn::kY)           xb |= XUSB_GAMEPAD_Y;

    // --- Shoulder buttons ---
    if (nb & btn::kLShoulder)   xb |= XUSB_GAMEPAD_LEFT_SHOULDER;
    if (nb & btn::kRShoulder)   xb |= XUSB_GAMEPAD_RIGHT_SHOULDER;

    // --- Menu buttons ---
    if (nb & btn::kOptions)     xb |= XUSB_GAMEPAD_START;
    if (nb & btn::kMenu)        xb |= XUSB_GAMEPAD_BACK;

    // --- Stick clicks ---
    if (nb & btn::kLStickClick) xb |= XUSB_GAMEPAD_LEFT_THUMB;
    if (nb & btn::kRStickClick) xb |= XUSB_GAMEPAD_RIGHT_THUMB;

    // --- D-Pad ---
    if (nb & btn::kDpadUp)      xb |= XUSB_GAMEPAD_DPAD_UP;
    if (nb & btn::kDpadDown)    xb |= XUSB_GAMEPAD_DPAD_DOWN;
    if (nb & btn::kDpadLeft)    xb |= XUSB_GAMEPAD_DPAD_LEFT;
    if (nb & btn::kDpadRight)   xb |= XUSB_GAMEPAD_DPAD_RIGHT;

    // --- Back paddles: MVP maps to unused Xbox buttons as defaults ---
    // L4/R4 are in ext, L5/R5 are in btn
    // These will be fully configurable later.
    // For now: no Xbox mapping (back paddles don't have Xbox equivalents)

    return xb;
}

} // namespace windeck::mapper
