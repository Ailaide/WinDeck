#pragma once

#include <cstdint>

namespace windeck::mapper {

// Maps Neptune 16-bit trigger to Xbox 8-bit trigger.
// MVP: simple linear scale, no dead zone.
class TriggerMapper {
public:
    uint8_t map(uint16_t raw) const;
};

} // namespace windeck::mapper
