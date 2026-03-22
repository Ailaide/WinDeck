#include "mapper/trigger_mapper.h"

namespace windeck::mapper {

uint8_t TriggerMapper::map(uint16_t raw) const {
    return apply_trigger_deadzone(raw, threshold_);
}

} // namespace windeck::mapper
