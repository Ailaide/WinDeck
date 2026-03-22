#pragma once

#include "neptune/neptune_report.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ViGEm/Client.h>
#include <cstdint>

namespace windeck::mapper {

// Flags for system button events (intercepted before remapping).
struct SystemButtonEvents {
    bool steam_pressed     = false;  // Steam button rising edge
    bool quick_access_pressed = false; // "..." button rising edge
};

// Maps Neptune button bitmasks (btn + btn_ext) to Xbox 360 wButtons bitmask.
// System buttons (Steam, "...") are intercepted and not passed to Xbox.
class ButtonMapper {
public:
    // Map Neptune buttons to Xbox wButtons field.
    // System button events are returned separately.
    USHORT map(uint32_t neptune_buttons, uint32_t neptune_buttons_ext,
               SystemButtonEvents& sys_events);

private:
    uint32_t prev_buttons_ = 0;
    uint32_t prev_buttons_ext_ = 0;
};

} // namespace windeck::mapper
