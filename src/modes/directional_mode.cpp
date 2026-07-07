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

    float nx = x / mag;
    float ny = y / mag;

    // Fold into [0, pi/2]: 0 = pure horizontal, pi/2 = pure vertical
    float abs_nx = std::abs(nx);
    float abs_ny = std::abs(ny);
    float fold = std::atan2(abs_ny, abs_nx);

    // Overlap zone centered on diagonal (pi/4), half-width = overlap * pi/4.
    // overlap=0: no zone, switch at diagonal. overlap=1: full quadrant is overlap.
    constexpr float kDiag = 0.7854f; // pi/4
    float half = overlap_ * kDiag;

    bool horiz, vert;
    if (half > 0.0f && std::abs(fold - kDiag) < half) {
        horiz = true;
        vert  = true;
    } else if (fold > kDiag) {
        horiz = false;
        vert  = true;
    } else {
        horiz = true;
        vert  = false;
    }

    apply(vert && ny > 0, vert && ny < 0,
          horiz && nx < 0, horiz && nx > 0, synth);
}

void DirectionalMode::update_discrete(bool up, bool down, bool left, bool right,
                                        windeck::output::SyntheticInput& synth) {
    apply(up, down, left, right, synth);
}

void DirectionalMode::release_all(windeck::output::SyntheticInput& /*synth*/) {
    // No direct release: by not declaring the keys, reconcile_holds() drops them
    // when no source wants them, and synth.release_all() clears them on mode
    // switch / QAM open / shutdown. Releasing here would clobber a key another
    // directional source is still holding.
    held_up_ = held_down_ = held_left_ = held_right_ = false;
}

void DirectionalMode::apply(bool up, bool down, bool left, bool right,
                              windeck::output::SyntheticInput& synth) {
    // Declare wanted keys only; SyntheticInput::reconcile_holds() presses and
    // releases them once per frame. This lets multiple directional sources
    // (D-pad + stick, both in WASD/Arrows) share the same keys without fighting:
    // a centered source no longer releases a held source's key every frame.
    if (up)    synth.key_hold(key_up_);
    if (down)  synth.key_hold(key_down_);
    if (left)  synth.key_hold(key_left_);
    if (right) synth.key_hold(key_right_);

    held_up_ = up;
    held_down_ = down;
    held_left_ = left;
    held_right_ = right;
}

} // namespace windeck::modes
