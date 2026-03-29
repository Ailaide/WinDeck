#pragma once

#include "config/profile.h"
#include "output/synthetic_input.h"
#include <array>

namespace windeck::modes {

// 8-sector radial menu from touchpad position.
// Sector 0 = 12 o'clock (north/top), going clockwise:
//   0=12h  1=1:30h  2=3h  3=4:30h  4=6h  5=7:30h  6=9h  7=10:30h
class RadialMenuMode {
public:
    // Set sector actions (call at profile load).
    void set_keys(const std::array<config::ButtonAction, 8>& keys);

    // Update with normalized touch position (-1..1, y+ = top of pad = north).
    // Returns active sector [0-7] or -1 (center dead zone / not touching).
    int update(float x, float y, windeck::output::SyntheticInput& synth);

    // Release held key and reset.
    void release_all(windeck::output::SyntheticInput& synth);

    int active_sector() const { return active_sector_; }

private:
    std::array<config::ButtonAction, 8> keys_{};
    int active_sector_ = -1;

    static int detect_sector(float x, float y);
    void emit(const config::ButtonAction& a, bool pressed,
              windeck::output::SyntheticInput& synth);
};

} // namespace windeck::modes
