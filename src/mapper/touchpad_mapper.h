#pragma once

#include "config/profile.h"
#include "neptune/neptune_device.h"
#include "modes/mouse_mode.h"
#include "modes/directional_mode.h"
#include "modes/inertia.h"
#include "output/synthetic_input.h"
#include <cstdint>

namespace windeck::mapper {

// Maps Neptune touchpad to trackpad/mouse/wasd/arrows/dpad/scroll/disabled.
class TouchpadMapper {
public:
    void set_name(const char* name) { name_ = name; }
    // Set reference to Neptune device for haptic feedback on click.
    void set_haptic_device(windeck::neptune::NeptuneDevice* device, uint8_t side) {
        haptic_device_ = device;
        haptic_side_ = side;
    }

    void apply_config(const config::TouchpadConfig& cfg);

    // Process touchpad input each frame.
    // pad_x, pad_y: absolute coordinates (-32768..32767), 0 when not touching
    // pressure: 0-255
    // touching: is finger on pad (from btn touch bit)
    void update(int16_t pad_x, int16_t pad_y, uint16_t pressure,
                bool touching, windeck::output::SyntheticInput& synth);

    // Tick inertia even when no new HID report (call every frame).
    void tick_inertia(windeck::output::SyntheticInput& synth);

    void release_all(windeck::output::SyntheticInput& synth);

private:
    config::TouchpadMode mode_ = config::TouchpadMode::Disabled;
    config::ButtonAction click_action_{};
    uint16_t pressure_threshold_ = 50;

    // Track previous state for delta calculation
    bool was_touching_ = false;
    int16_t prev_x_ = 0;
    int16_t prev_y_ = 0;

    // Velocity tracking for inertia (last N deltas)
    float vel_x_ = 0.0f;
    float vel_y_ = 0.0f;

    // Click state
    bool click_held_ = false;

    modes::MouseMode mouse_;
    modes::DirectionalMode directional_;
    modes::Inertia inertia_;
    const char* name_ = "pad";
    windeck::neptune::NeptuneDevice* haptic_device_ = nullptr;
    uint8_t haptic_side_ = 0;  // 0=right, 1=left

    void click_haptic();
    void emit_action(const config::ButtonAction& action, bool pressed,
                      windeck::output::SyntheticInput& synth);
};

} // namespace windeck::mapper
