#include "mapper/stick_mapper.h"
#include "mapper/dead_zone.h"

namespace windeck::mapper {

void StickMapper::map(int16_t raw_x, int16_t raw_y,
                       int16_t& out_x, int16_t& out_y) const {
    apply_stick_deadzone(raw_x, raw_y, out_x, out_y, inner_, outer_, curve_);
}

} // namespace windeck::mapper
