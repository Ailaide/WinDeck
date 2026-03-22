#pragma once

#include "config/profile.h"
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace windeck::mapper {

// Use the canonical ResponseCurve from config
using config::ResponseCurve;

// Apply response curve to a normalized magnitude [0, 1] → [0, 1]
inline float apply_curve(float t, ResponseCurve curve) {
    switch (curve) {
        case ResponseCurve::Quadratic:        return t * t;
        case ResponseCurve::InverseQuadratic: return 1.0f - (1.0f - t) * (1.0f - t);
        default:                              return t; // Linear
    }
}

// Apply radial dead zone + response curve to a stick axis pair.
// inner/outer: normalized thresholds [0, 1]
// Returns scaled values in [-32768, 32767].
inline void apply_stick_deadzone(int16_t raw_x, int16_t raw_y,
                                  int16_t& out_x, int16_t& out_y,
                                  float inner, float outer,
                                  ResponseCurve curve) {
    float fx = static_cast<float>(raw_x) / 32767.0f;
    float fy = static_cast<float>(raw_y) / 32767.0f;
    float mag = std::sqrt(fx * fx + fy * fy);

    if (mag < inner || mag < 1e-6f) {
        out_x = 0;
        out_y = 0;
        return;
    }

    float range = outer - inner;
    if (range < 1e-6f) { out_x = 0; out_y = 0; return; }

    float clamped = (mag < outer) ? mag : outer;
    float normalized = (clamped - inner) / range;
    float curved = apply_curve(normalized, curve);
    float factor = curved / mag;

    out_x = static_cast<int16_t>(std::clamp(fx * factor * 32767.0f, -32768.0f, 32767.0f));
    out_y = static_cast<int16_t>(std::clamp(fy * factor * 32767.0f, -32768.0f, 32767.0f));
}

// Scale a 16-bit trigger value (0-32767) to 8-bit (0-255) with threshold dead zone.
inline uint8_t apply_trigger_deadzone(uint16_t raw, float threshold) {
    float normalized = static_cast<float>(raw) / 32767.0f;
    if (normalized < threshold) return 0;
    float denom = 1.0f - threshold;
    if (denom < 1e-6f) return 255;
    float scaled = (normalized - threshold) / denom;
    return static_cast<uint8_t>(std::clamp(scaled * 255.0f, 0.0f, 255.0f));
}

} // namespace windeck::mapper
