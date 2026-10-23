#pragma once

#include "mapper/dead_zone.h"
#include "config/profile.h"
#include "output/synthetic_input.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ViGEm/Client.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

namespace windeck::mapper {

// What one trigger produced in a pass.
struct TriggerFrame {
    uint8_t axis = 0;           // analog Xbox trigger; 0 in Buttons mode
    USHORT  xbox_buttons = 0;   // Xbox bits its bindings hold right now
    config::ClickStrength click = config::ClickStrength::None;  // strongest click of this pass
};

// One physical trigger: an analog Xbox trigger, or bindings A, B and C that
// press at their own depths (see config::TriggerConfig).
//
// Keyboard and mouse-button bindings go to this trigger's own hold set in
// SyntheticInput; Xbox bits and the click are returned for the caller to merge
// and send. Time comes from the caller, so behaviour does not depend on the
// report rate and tests can drive it exactly.
class TriggerMapper {
public:
    using Clock = std::chrono::steady_clock;

    // `source` is the hold set this trigger's keys and mouse buttons go to.
    explicit TriggerMapper(windeck::output::HoldSource source) : source_(source) {}

    // New config: forgets every binding state and disarms the trigger.
    void apply_config(const config::TriggerConfig& cfg);

    // A fresh report. Taps whose time is up end first, then the pull is applied.
    TriggerFrame update(uint16_t raw, Clock::time_point now, windeck::output::SyntheticInput& synth);

    // No fresh report: only ends taps whose time is up. Never clicks.
    TriggerFrame tick(Clock::time_point now, windeck::output::SyntheticInput& synth);

    // When the earliest running tap ends, if any.
    std::optional<Clock::time_point> next_deadline() const;

    // Forced release (menu open, profile change, shutdown): drops every held
    // output and running tap and disarms the trigger. Never clicks.
    void release_all(windeck::output::SyntheticInput& synth);

    // Disarmed after start-up, a new config or a forced release, until a
    // report shows the pull at or below config::kTriggerArmPull.
    bool armed() const { return armed_; }

private:
    struct BindingState {
        // Hold: the action is pressed. Immediate: fired; waits to re-arm.
        bool active = false;
        // Immediate: when the running tap ends.
        std::optional<Clock::time_point> tap_end;
    };

    windeck::output::HoldSource source_;
    config::TriggerConfig cfg_;
    std::array<BindingState, config::kTriggerBindings> state_{};
    bool armed_ = false;       // a new trigger is disarmed until released
    float last_pull_ = 0.0f;   // pull of the latest report
    uint8_t axis_ = 0;         // analog value of the latest report

    void expire(Clock::time_point now);
    TriggerFrame outputs(config::ClickStrength click, windeck::output::SyntheticInput& synth) const;
};

} // namespace windeck::mapper
