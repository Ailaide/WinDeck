#include "modes/inertia.h"
#include <cmath>

namespace windeck::modes {

namespace {
constexpr float kReferenceTickSeconds = 1.0f / 250.0f;
}

void Inertia::set_velocity(float vx, float vy) {
    vx_ = vx;
    vy_ = vy;
}

void Inertia::begin() {
    float mag = std::sqrt(vx_ * vx_ + vy_ * vy_);
    active_ = (mag > cutoff_);
    has_last_tick_ = false;
}

bool Inertia::tick(float& out_dx, float& out_dy) {
    if (!active_) {
        out_dx = 0.0f;
        out_dy = 0.0f;
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    float step = 1.0f;
    if (has_last_tick_) {
        float dt = std::chrono::duration<float>(now - last_tick_).count();
        if (dt > 0.0f) {
            step = dt / kReferenceTickSeconds;
        }
    }
    last_tick_ = now;
    has_last_tick_ = true;

    out_dx = vx_ * step;
    out_dy = vy_ * step;

    float decay = std::pow(friction_, step);
    vx_ *= decay;
    vy_ *= decay;

    float mag = std::sqrt(vx_ * vx_ + vy_ * vy_);
    if (mag < cutoff_) {
        active_ = false;
        vx_ = 0.0f;
        vy_ = 0.0f;
        has_last_tick_ = false;
    }

    return active_;
}

void Inertia::stop() {
    active_ = false;
    vx_ = 0.0f;
    vy_ = 0.0f;
    has_last_tick_ = false;
}

} // namespace windeck::modes
