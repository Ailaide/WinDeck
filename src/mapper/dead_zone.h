#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>

namespace windeck::mapper {

// Apply radial dead zone to a stick axis pair.
// Returns scaled values in [-32768, 32767].
inline void apply_stick_deadzone(int16_t raw_x, int16_t raw_y,
                                  int16_t& out_x, int16_t& out_y,
                                  float inner = 0.05f, float outer = 0.95f) {
    float fx = static_cast<float>(raw_x) / 32767.0f;
    float fy = static_cast<float>(raw_y) / 32767.0f;
    float mag = std::sqrt(fx * fx + fy * fy);

    if (mag < inner || mag < 1e-6f) {
        out_x = 0;
        out_y = 0;
        return;
    }

    float clamped_mag = std::min(mag, outer);
    float scaled = (clamped_mag - inner) / (outer - inner);
    float factor = scaled / mag;

    float result_x = fx * factor;
    float result_y = fy * factor;

    out_x = static_cast<int16_t>(std::clamp(result_x * 32767.0f, -32768.0f, 32767.0f));
    out_y = static_cast<int16_t>(std::clamp(result_y * 32767.0f, -32768.0f, 32767.0f));
}

// Scale a 16-bit trigger value (0-32767) to 8-bit (0-255) with threshold dead zone.
inline uint8_t apply_trigger_deadzone(uint16_t raw, float threshold = 0.02f) {
    float normalized = static_cast<float>(raw) / 32767.0f;
    if (normalized < threshold) return 0;
    float scaled = (normalized - threshold) / (1.0f - threshold);
    return static_cast<uint8_t>(std::clamp(scaled * 255.0f, 0.0f, 255.0f));
}

} // namespace windeck::mapper
