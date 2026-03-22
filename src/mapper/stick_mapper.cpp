#include "mapper/stick_mapper.h"

namespace windeck::mapper {

void StickMapper::map(int16_t raw_x, int16_t raw_y,
                       int16_t& out_x, int16_t& out_y) const {
    // MVP: direct passthrough, no dead zone
    out_x = raw_x;
    out_y = raw_y;
}

} // namespace windeck::mapper
