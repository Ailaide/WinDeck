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
#include "output/xbox_state.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ViGEm/Client.h>
#include <array>
#include <chrono>
#include <optional>
#include <shared_mutex>

namespace windeck::mapper {

struct MapResult {
    XUSB_REPORT xbox_report{};
    SystemButtonEvents sys_events;
    // Haptic click for each trigger side (0 = left, 1 = right), sent by the
    // caller after mapping, outside the mapper lock.
    std::array<config::ClickStrength, 2> clicks{config::ClickStrength::None,
                                                config::ClickStrength::None};
};

class Mapper {
public:
    using Clock = std::chrono::steady_clock;

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

    // No new report: tick touchpad inertia and end trigger taps whose time is up.
    void tick(windeck::output::SyntheticInput& synth);

    // tick() + reconcile + flush under one lock. Returns the Xbox report with
    // the triggers' current bits merged into every other source's last state.
    XUSB_REPORT tick_and_flush(windeck::output::SyntheticInput& synth);

    // When the earliest running trigger tap ends, if any (caps the read wait).
    std::optional<Clock::time_point> next_deadline() const;

    // After the menu closes: take the current physical buttons as already seen,
    // so a button held through the menu keeps its hold but fires no wheel tick.
    void resync_buttons(uint32_t buttons, uint32_t buttons_ext);

    // Forced release (menu open, reconnect, shutdown): every mapper and every
    // hold set is cleared and the releases are sent, all under the exclusive lock.
    void release_all(windeck::output::SyntheticInput& synth);

private:
    MapResult map_unlocked(const neptune::InputReport& input,
                           windeck::output::SyntheticInput& synth,
                           Clock::time_point now);
    void tick_unlocked(windeck::output::SyntheticInput& synth, Clock::time_point now);
    void release_all_unlocked(windeck::output::SyntheticInput& synth);
    windeck::output::XboxState xbox_;
    mutable std::shared_mutex mutex_;
    using HoldSource = windeck::output::HoldSource;
    ButtonMapper   button_mapper_;
    StickMapper    left_stick_{HoldSource::LeftStick};
    StickMapper    right_stick_{HoldSource::RightStick};
    TriggerMapper  left_trigger_{HoldSource::LeftTrigger};
    TriggerMapper  right_trigger_{HoldSource::RightTrigger};
    DpadMapper     dpad_;
    TouchpadMapper left_touchpad_{HoldSource::LeftPadDirectional, HoldSource::LeftPadClick,
                                  HoldSource::LeftPadRadial};
    TouchpadMapper right_touchpad_{HoldSource::RightPadDirectional, HoldSource::RightPadClick,
                                   HoldSource::RightPadRadial};
    GyroMapper     gyro_;
};

} // namespace windeck::mapper
