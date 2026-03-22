#include "modes/mouse_mode.h"
#include <cmath>

namespace windeck::modes {

void MouseMode::update_stick(float x, float y, windeck::output::SyntheticInput& synth) {
    // Stick range [-1, 1] → mouse pixels per frame
    // Base speed: ~15 pixels/frame at full deflection, scaled by sensitivity
    constexpr float kBaseSpeed = 15.0f;
    float dx = x * kBaseSpeed * sensitivity_;
    float dy = -y * kBaseSpeed * sensitivity_; // Invert Y: stick up = cursor up (negative screen Y)

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
