#pragma once

#include "mapper/dead_zone.h"
#include <cstdint>

namespace windeck::mapper {

// Maps Neptune 16-bit trigger to Xbox 8-bit trigger with threshold dead zone.
class TriggerMapper {
public:
    void set_threshold(float threshold) {
        threshold_ = threshold;
    }

    uint8_t map(uint16_t raw) const;

private:
    float threshold_ = 0.02f;
};

} // namespace windeck::mapper
