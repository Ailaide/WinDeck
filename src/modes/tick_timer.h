#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>

namespace windeck::modes {

// Measures elapsed time between ticks, normalized to 250Hz reference rate.
// step() returns 1.0 when called at exactly 250Hz (4ms intervals).
// Clamped to max_step to avoid one-shot jumps after long stalls.
class TickTimer {
public:
    float step(float max_step = 8.0f) {
        auto now = std::chrono::steady_clock::now();
        float s = 1.0f;
        if (has_last_) {
            float dt = std::chrono::duration<float>(now - last_).count();
            if (dt > 0.0f) {
                s = std::clamp(dt / kReferenceTickSeconds, 0.0f, max_step);
            }
        }
        last_ = now;
        has_last_ = true;
        return s;
    }

    void reset() { has_last_ = false; }

private:
    static constexpr float kReferenceTickSeconds = 1.0f / 250.0f;
    bool has_last_ = false;
    std::chrono::steady_clock::time_point last_{};
};

// Exponential smoothing alpha for a given step and half-life (in 250Hz ticks).
// Usage: value += (target - value) * smooth_alpha(step, half_life);
inline float smooth_alpha(float step, float half_life_ticks = 1.0f) {
    return 1.0f - std::pow(0.5f, step / half_life_ticks);
}

} // namespace windeck::modes
