#include "mapper/trigger_mapper.h"
#include <algorithm>

namespace windeck::mapper {

uint8_t TriggerMapper::map(uint16_t raw) const {
    // Neptune triggers: 0-32767, Xbox triggers: 0-255
    // Simple linear scale using top 8 bits
    float normalized = static_cast<float>(raw) / 32767.0f;
    return static_cast<uint8_t>(std::clamp(normalized * 255.0f, 0.0f, 255.0f));
}

} // namespace windeck::mapper
