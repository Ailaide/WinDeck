#include "modes/directional_mode.h"
#include <cmath>

namespace windeck::modes {

void DirectionalMode::set_keys_wasd() {
    key_up_ = 0x57;    // W
    key_down_ = 0x53;  // S
    key_left_ = 0x41;  // A
    key_right_ = 0x44; // D
}

void DirectionalMode::set_keys_arrows() {
    key_up_ = VK_UP;
    key_down_ = VK_DOWN;
    key_left_ = VK_LEFT;
    key_right_ = VK_RIGHT;
}

void DirectionalMode::update_analog(float x, float y, float threshold,
                                      windeck::output::SyntheticInput& synth) {
    float mag = std::sqrt(x * x + y * y);
    if (mag < threshold) {
        apply(false, false, false, false, synth);
        return;
    }

    // Normalize
    float nx = x / mag;
    float ny = y / mag;

    // Base zone: 45 degrees per direction (no overlap).
    // overlap_ widens each zone. At overlap=1.0, each zone is 90 degrees
    // (full quadrant), meaning diagonals always trigger two keys.
    //
    // Primary axis threshold: cos(22.5° + overlap * 22.5°)
    // At overlap=0: cos(22.5°) ≈ 0.924 — must be strongly in one direction
    // At overlap=1: cos(45°) ≈ 0.707 — standard quadrant division
    float half_zone = 0.3927f + overlap_ * 0.3927f; // 22.5° to 45° in radians
    float cos_thresh = std::cos(half_zone);

    bool up    = ny > cos_thresh;
    bool down  = ny < -cos_thresh;
    bool right = nx > cos_thresh;
    bool left  = nx < -cos_thresh;

    // With overlap, check if we're in the overlap zone (diagonal)
    if (overlap_ > 0.0f) {
        float diag_thresh = std::cos(half_zone);
        // In overlap zone, both axes can trigger if they exceed a lower threshold
        float secondary_thresh = std::sin(half_zone);
        if (!up && ny > secondary_thresh) up = (left || right);
        if (!down && ny < -secondary_thresh) down = (left || right);
        if (!right && nx > secondary_thresh) right = (up || down);
        if (!left && nx < -secondary_thresh) left = (up || down);
    }

    apply(up, down, left, right, synth);
}

void DirectionalMode::update_discrete(bool up, bool down, bool left, bool right,
                                        windeck::output::SyntheticInput& synth) {
    apply(up, down, left, right, synth);
}

void DirectionalMode::release_all(windeck::output::SyntheticInput& synth) {
    apply(false, false, false, false, synth);
}

void DirectionalMode::apply(bool up, bool down, bool left, bool right,
                              windeck::output::SyntheticInput& synth) {
    synth.key_set(key_up_, up);
    synth.key_set(key_down_, down);
    synth.key_set(key_left_, left);
    synth.key_set(key_right_, right);

    held_up_ = up;
    held_down_ = down;
    held_left_ = left;
    held_right_ = right;
}

} // namespace windeck::modes
