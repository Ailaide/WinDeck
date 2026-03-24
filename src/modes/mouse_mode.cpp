#include "modes/mouse_mode.h"

namespace windeck::modes {

namespace {
constexpr float kReferenceTickSeconds = 1.0f / 250.0f;
}

void MouseMode::update_stick(float x, float y, windeck::output::SyntheticInput& synth) {
    // Stick range [-1, 1] -> mouse pixels at 250Hz reference rate.
    // Time scaling keeps cursor speed stable if input tick rate changes.
    constexpr float kBaseSpeed = 15.0f;

    auto now = std::chrono::steady_clock::now();
    float step = 1.0f;
    if (has_last_stick_tick_) {
        float dt = std::chrono::duration<float>(now - last_stick_tick_).count();
        if (dt > 0.0f) {
            step = dt / kReferenceTickSeconds;
        }
    }
    last_stick_tick_ = now;
    has_last_stick_tick_ = true;

    float dx = x * kBaseSpeed * sensitivity_ * step;
    float dy = -y * kBaseSpeed * sensitivity_ * step; // Invert Y: stick up = cursor up (negative screen Y)

    accum_x_ += dx;
    accum_y_ += dy;

    int ix = static_cast<int>(accum_x_);
    int iy = static_cast<int>(accum_y_);

    if (ix != 0 || iy != 0) {
        synth.mouse_move(ix, iy);
        accum_x_ -= static_cast<float>(ix);
        accum_y_ -= static_cast<float>(iy);
    }
}

void MouseMode::update_delta(float dx, float dy, windeck::output::SyntheticInput& synth) {
    accum_x_ += dx * sensitivity_;
    accum_y_ += dy * sensitivity_;

    int ix = static_cast<int>(accum_x_);
    int iy = static_cast<int>(accum_y_);

    if (ix != 0 || iy != 0) {
        synth.mouse_move(ix, iy);
        accum_x_ -= static_cast<float>(ix);
        accum_y_ -= static_cast<float>(iy);
    }
}

} // namespace windeck::modes
