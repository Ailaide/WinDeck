#pragma once

#include <cstdint>

namespace windeck::mapper {

// Maps Neptune stick axes to Xbox stick axes.
// MVP: passthrough with no dead zone (dead zones come later).
class StickMapper {
public:
    void map(int16_t raw_x, int16_t raw_y,
             int16_t& out_x, int16_t& out_y) const;
};

} // namespace windeck::mapper
