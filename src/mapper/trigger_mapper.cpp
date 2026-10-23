#include "mapper/trigger_mapper.h"

#include <algorithm>

namespace windeck::mapper {

using config::ActionType;
using config::BindingMode;
using config::ClickStrength;

namespace {

ClickStrength stronger(ClickStrength a, ClickStrength b) {
    return static_cast<uint8_t>(a) >= static_cast<uint8_t>(b) ? a : b;
}

} // namespace

void TriggerMapper::apply_config(const config::TriggerConfig& cfg) {
    cfg_ = cfg;
    state_ = {};
    armed_ = false;
    axis_ = 0;
}

TriggerFrame TriggerMapper::update(uint16_t raw, Clock::time_point now,
                                   windeck::output::SyntheticInput& synth) {
    const float pull = std::clamp(static_cast<float>(raw) / 32767.0f, 0.0f, 1.0f);

    if (cfg_.mode == config::TriggerMode::Xbox) {
        last_pull_ = pull;
        axis_ = apply_trigger_deadzone(raw, cfg_.deadzone);
        return outputs(ClickStrength::None, synth);
    }

    // A deadline that passed before this report is handled first, against the
    // pull of the report before it.
    expire(now);
    last_pull_ = pull;
    axis_ = 0;

    if (!armed_) {
        if (pull > config::kTriggerArmPull) return outputs(ClickStrength::None, synth);
        armed_ = true;
    }

    ClickStrength click = ClickStrength::None;
    for (size_t i = 0; i < config::kTriggerBindings; i++) {
        const auto& b = cfg_.bindings[i];
        auto& s = state_[i];
        if (b.action.type == ActionType::None) { s = {}; continue; }

        bool pressed = false, released = false;
        if (b.mode == BindingMode::Hold) {
            if (!s.active && pull >= b.pull) {
                s.active = pressed = true;
            } else if (s.active && pull <= b.pull - config::kTriggerReleaseGap) {
                s.active = false;
                released = true;
            }
        } else {
            // Immediate: no release gap. Once its tap has ended, it re-arms as
            // soon as the pull is below the depth; the tap length is what stops
            // a trigger resting on the line from tapping again at once.
            if (s.active && !s.tap_end && pull < b.pull) s.active = false;
            if (!s.active && pull >= b.pull) {
                s.active = pressed = true;
                s.tap_end = now + std::chrono::milliseconds(config::kImmediateTapMs);
            }
        }

        if (pressed && b.action.type == ActionType::MouseScroll)
            synth.mouse_scroll(b.action.code ? WHEEL_DELTA : -WHEEL_DELTA);
        if (pressed || released) click = stronger(click, b.click);
    }
    return outputs(click, synth);
}

TriggerFrame TriggerMapper::tick(Clock::time_point now, windeck::output::SyntheticInput& synth) {
    if (cfg_.mode == config::TriggerMode::Buttons) expire(now);
    return outputs(ClickStrength::None, synth);
}

std::optional<TriggerMapper::Clock::time_point> TriggerMapper::next_deadline() const {
    std::optional<Clock::time_point> earliest;
    for (const auto& s : state_)
        if (s.tap_end && (!earliest || *s.tap_end < *earliest)) earliest = s.tap_end;
    return earliest;
}

void TriggerMapper::release_all(windeck::output::SyntheticInput& synth) {
    state_ = {};
    armed_ = false;
    axis_ = 0;
    synth.clear_holds(source_);
}

void TriggerMapper::expire(Clock::time_point now) {
    for (size_t i = 0; i < config::kTriggerBindings; i++) {
        auto& s = state_[i];
        if (!s.tap_end || *s.tap_end > now) continue;
        s.tap_end.reset();
        // Re-arm at once if the latest report already showed the pull below the
        // depth; otherwise the next report below it re-arms.
        if (last_pull_ < cfg_.bindings[i].pull) s.active = false;
    }
}

TriggerFrame TriggerMapper::outputs(ClickStrength click,
                                    windeck::output::SyntheticInput& synth) const {
    TriggerFrame f;
    f.axis = axis_;
    f.click = click;

    windeck::output::HoldSet holds;
    if (cfg_.mode == config::TriggerMode::Buttons) {
        for (size_t i = 0; i < config::kTriggerBindings; i++) {
            const auto& b = cfg_.bindings[i];
            const auto& s = state_[i];
            const bool wanted = (b.mode == BindingMode::Hold) ? s.active : s.tap_end.has_value();
            if (!wanted) continue;
            if (b.action.type == ActionType::XboxButton) f.xbox_buttons |= b.action.code;
            else holds.add(b.action);   // keys and mouse buttons; wheel ticks are one-off
        }
    }
    synth.set_holds(source_, holds);
    return f;
}

} // namespace windeck::mapper
