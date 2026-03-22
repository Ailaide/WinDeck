#pragma once

#include "neptune/neptune_report.h"
#include "config/profile.h"
#include "mapper/button_mapper.h"
#include "mapper/stick_mapper.h"
#include "mapper/trigger_mapper.h"
#include "output/synthetic_input.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ViGEm/Client.h>

namespace windeck::mapper {

struct MapResult {
    XUSB_REPORT xbox_report;
    SystemButtonEvents sys_events;
};

class Mapper {
public:
    // Apply a profile configuration to all sub-mappers.
    void apply_profile(const config::Profile& profile);

    // Transform Neptune input into Xbox 360 report + system events.
    // Also emits keyboard/mouse actions via synth.
    MapResult map(const neptune::InputReport& input,
                  windeck::output::SyntheticInput& synth);

private:
    ButtonMapper  button_mapper_;
    StickMapper   left_stick_;
    StickMapper   right_stick_;
    TriggerMapper left_trigger_;
    TriggerMapper right_trigger_;
};

} // namespace windeck::mapper
