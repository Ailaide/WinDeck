#pragma once

namespace windeck::overlay { class RadialOverlay; }

#include "neptune/neptune_report.h"
#include "neptune/neptune_device.h"
#include "config/profile.h"
#include "mapper/button_mapper.h"
#include "mapper/stick_mapper.h"
#include "mapper/trigger_mapper.h"
#include "mapper/dpad_mapper.h"
#include "mapper/touchpad_mapper.h"
#include "mapper/gyro_mapper.h"
#include "output/synthetic_input.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ViGEm/Client.h>
#include <shared_mutex>

namespace windeck::mapper {

struct MapResult {
    XUSB_REPORT xbox_report;
    SystemButtonEvents sys_events;
};

class Mapper {
public:
    void set_device(windeck::neptune::NeptuneDevice* device);
    void apply_profile(const config::Profile& profile,
                       windeck::output::SyntheticInput& synth);
    void set_overlay(windeck::overlay::RadialOverlay* overlay);

    MapResult map(const neptune::InputReport& input,
                  windeck::output::SyntheticInput& synth);

    // Map + tick inertia + flush synth, all under one lock.
    // Prevents race with apply_profile on tray/overlay thread.
    MapResult map_and_flush(const neptune::InputReport& input,
                            windeck::output::SyntheticInput& synth);

    // Tick inertia for touchpads (call every frame).
    void tick(windeck::output::SyntheticInput& synth);

    // Tick inertia + flush under one lock.
    void tick_and_flush(windeck::output::SyntheticInput& synth);

    void release_all(windeck::output::SyntheticInput& synth);

private:
    MapResult map_unlocked(const neptune::InputReport& input,
                           windeck::output::SyntheticInput& synth);
    void release_all_unlocked(windeck::output::SyntheticInput& synth);
    mutable std::shared_mutex mutex_;
    ButtonMapper   button_mapper_;
    StickMapper    left_stick_;
    StickMapper    right_stick_;
    TriggerMapper  left_trigger_;
    TriggerMapper  right_trigger_;
    DpadMapper     dpad_;
    TouchpadMapper left_touchpad_;
    TouchpadMapper right_touchpad_;
    GyroMapper     gyro_;
};

} // namespace windeck::mapper
