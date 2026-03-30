#pragma once

#include "config/profile.h"
#include "modes/directional_mode.h"
#include "modes/mouse_mode.h"
#include "output/synthetic_input.h"
#include <cstdint>

namespace windeck::mapper {

class StickMapper {
public:
    void apply_config(const config::StickConfig& cfg);

    void map(int16_t raw_x, int16_t raw_y,
             int16_t& out_x, int16_t& out_y,
             windeck::output::SyntheticInput& synth);

    void release_all(windeck::output::SyntheticInput& synth);

private:
    config::StickMode mode_ = config::StickMode::Stick;
    float inner_ = 0.05f;
    float outer_ = 0.95f;
    config::ResponseCurve curve_ = config::ResponseCurve::Linear;
    bool invert_x_ = false;
    bool invert_y_ = false;

    modes::DirectionalMode directional_;
    modes::MouseMode mouse_;
};

} // namespace windeck::mapper
