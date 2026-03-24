#pragma once

#include "mapper/dead_zone.h"
#include "config/profile.h"
#include "output/synthetic_input.h"
#include <cstdint>

namespace windeck::mapper {

// Maps Neptune 16-bit trigger to Xbox 8-bit trigger with threshold dead zone.
// Optionally fires a button action when trigger crosses a threshold.
class TriggerMapper {
public:
    void apply_config(const config::TriggerConfig& cfg) {
        threshold_ = cfg.threshold;
        has_button_ = cfg.has_button;
        button_threshold_ = cfg.button_threshold;
        button_action_ = cfg.button_action;
    }

    void set_threshold(float threshold) {
        threshold_ = threshold;
    }

    uint8_t map(uint16_t raw, windeck::output::SyntheticInput& synth);

private:
    float threshold_ = 0.02f;
    bool has_button_ = false;
    float button_threshold_ = 0.5f;
    config::ButtonAction button_action_{};
    bool button_pressed_ = false;
};

} // namespace windeck::mapper
