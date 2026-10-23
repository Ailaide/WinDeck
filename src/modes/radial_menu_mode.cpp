#include "modes/radial_menu_mode.h"
#include <cmath>

namespace windeck::modes {

static constexpr float kPi        = 3.14159265f;
static constexpr float kDeadZoneR = 0.25f;

void RadialMenuMode::set_keys(const std::array<config::ButtonAction, 8>& keys) {
    keys_ = keys;
}

// Returns 0-7 (clockwise from 12 o'clock) or -1 if inside dead zone.
// x = right(+), y = top(+). atan2(x,y) = angle from north CW.
int RadialMenuMode::detect_sector(float x, float y) {
    if (std::sqrt(x * x + y * y) < kDeadZoneR) return -1;
    float angle = std::atan2(x, y);               // [-pi, pi], 0 = north, CW+
    if (angle < 0.0f) angle += 2.0f * kPi;        // [0, 2pi)
    angle = std::fmod(angle + kPi / 8.0f, 2.0f * kPi); // offset so sector 0 is centered at north
    int s = static_cast<int>(angle / (kPi / 4.0f));
    return (s < 0) ? 0 : (s > 7) ? 7 : s;
}

int RadialMenuMode::update(float x, float y, windeck::output::SyntheticInput& synth) {
    int s = detect_sector(x, y);
    if (s != active_sector_) {
        // The hovered sector's key or mouse button is this source's whole set.
        windeck::output::HoldSet holds;
        if (s >= 0) holds.add(keys_[s]);
        synth.set_holds(source_, holds);
        active_sector_ = s;
    }
    return active_sector_;
}

void RadialMenuMode::release_all(windeck::output::SyntheticInput& synth) {
    synth.clear_holds(source_);
    active_sector_ = -1;
}

} // namespace windeck::modes
