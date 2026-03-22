#pragma once

#include "neptune/neptune_report.h"
#include "mapper/button_mapper.h"
#include "mapper/stick_mapper.h"
#include "mapper/trigger_mapper.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ViGEm/Client.h>

namespace windeck::mapper {

// Result of mapping a single frame.
struct MapResult {
    XUSB_REPORT xbox_report;
    SystemButtonEvents sys_events;
};

// Main mapper: orchestrates all sub-mappers to produce an XUSB_REPORT.
class Mapper {
public:
    // Transform Neptune input into Xbox 360 report + system events.
    MapResult map(const neptune::InputReport& input);

private:
    ButtonMapper  button_mapper_;
    StickMapper   left_stick_;
    StickMapper   right_stick_;
    TriggerMapper left_trigger_;
    TriggerMapper right_trigger_;
};

} // namespace windeck::mapper
