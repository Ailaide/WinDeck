#pragma once

#include "config/profile.h"
#include "modes/directional_mode.h"
#include "output/synthetic_input.h"
#include <cstdint>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ViGEm/Client.h>

namespace windeck::mapper {

// Maps Neptune D-Pad to Xbox D-Pad / WASD / arrows.
class DpadMapper {
public:
    void apply_config(const config::DpadConfig& cfg);

    // Returns Xbox wButtons bits for D-Pad (mode=dpad),
    // or emits keys via synth (mode=wasd/arrows) and returns 0.
    USHORT map(uint32_t neptune_buttons,
               windeck::output::SyntheticInput& synth);

    void release_all(windeck::output::SyntheticInput& synth);

private:
    config::DpadMode mode_ = config::DpadMode::Dpad;
    modes::DirectionalMode directional_;
};

} // namespace windeck::mapper
