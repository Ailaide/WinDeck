#pragma once

#include "output/synthetic_input.h"
#include <cstdint>

namespace windeck::modes {

// Converts a 2D direction (analog stick, touchpad zone, dpad) into
// 4 key presses (WASD or arrow keys). Reusable across StickMapper,
// DpadMapper, and TouchpadMapper.
//
// Overlap controls diagonal behavior:
//   0.0 = strict 4 zones, no diagonals
//   1.0 = maximum overlap, diagonals trigger easily
class DirectionalMode {
public:
    // `source` is the hold set this mode writes; each control has its own.
    explicit DirectionalMode(windeck::output::HoldSource source) : source_(source) {}

    // Set the 4 VK codes for up/down/left/right
    void set_keys_wasd();   // W=0x57, A=0x41, S=0x53, D=0x44
    void set_keys_arrows(); // VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT
    void set_overlap(float overlap) { overlap_ = overlap; }

    // Update from normalized analog input (-1..1 per axis).
    // Threshold: minimum magnitude to register any direction.
    void update_analog(float x, float y, float threshold,
                        windeck::output::SyntheticInput& synth);

    // Update from 4 discrete booleans (dpad).
    void update_discrete(bool up, bool down, bool left, bool right,
                          windeck::output::SyntheticInput& synth);

    // Hold nothing any more (finger lifted, mode switch, profile change).
    void release_all(windeck::output::SyntheticInput& synth);

private:
    windeck::output::HoldSource source_;
    uint16_t key_up_    = 0x57; // W
    uint16_t key_down_  = 0x53; // S
    uint16_t key_left_  = 0x41; // A
    uint16_t key_right_ = 0x44; // D
    float overlap_ = 0.0f;

    void apply(bool up, bool down, bool left, bool right,
               windeck::output::SyntheticInput& synth);
};

} // namespace windeck::modes
