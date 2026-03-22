#pragma once

#include "config/profile.h"
#include <cstdint>

namespace windeck::mapper {

// Maps Neptune stick axes to Xbox stick axes with dead zone and response curve.
class StickMapper {
public:
    void set_deadzone(float inner, float outer) {
        inner_ = inner;
        outer_ = outer;
    }

    void set_response_curve(config::ResponseCurve curve) {
        curve_ = curve;
    }

    void map(int16_t raw_x, int16_t raw_y,
             int16_t& out_x, int16_t& out_y) const;

private:
    float inner_ = 0.05f;
    float outer_ = 0.95f;
    config::ResponseCurve curve_ = config::ResponseCurve::Linear;
};

} // namespace windeck::mapper
