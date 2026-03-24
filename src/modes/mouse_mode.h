#pragma once

#include "output/synthetic_input.h"
#include <chrono>

namespace windeck::modes {

// Converts analog input (stick deflection, touchpad delta, gyro angular velocity)
// into mouse cursor movement. Reusable across StickMapper, TouchpadMapper, GyroMapper.
class MouseMode {
public:
    void set_sensitivity(float sens) { sensitivity_ = sens; }

    // Update from normalized stick input (-1..1). Applies dead zone externally.
    // Output is accumulated mouse delta for the current update tick.
    void update_stick(float x, float y, windeck::output::SyntheticInput& synth);

    // Update from raw touchpad/gyro delta (already in pixel-ish units).
    void update_delta(float dx, float dy, windeck::output::SyntheticInput& synth);

private:
    float sensitivity_ = 1.0f;

    // Sub-pixel accumulator for smooth movement at low speeds
    float accum_x_ = 0.0f;
    float accum_y_ = 0.0f;

    // Time base for stick->mouse conversion.
    bool has_last_stick_tick_ = false;
    std::chrono::steady_clock::time_point last_stick_tick_{};
};

} // namespace windeck::modes
