#include "modes/inertia.h"
#include <cmath>

namespace windeck::modes {

void Inertia::set_velocity(float vx, float vy) {
    vx_ = vx;
    vy_ = vy;
}

void Inertia::begin() {
    float mag = std::sqrt(vx_ * vx_ + vy_ * vy_);
    active_ = (mag > cutoff_);
}

bool Inertia::tick(float& out_dx, float& out_dy) {
    if (!active_) {
        out_dx = 0.0f;
        out_dy = 0.0f;
        return false;
    }

    out_dx = vx_;
    out_dy = vy_;

    vx_ *= friction_;
    vy_ *= friction_;

    float mag = std::sqrt(vx_ * vx_ + vy_ * vy_);
    if (mag < cutoff_) {
        active_ = false;
        vx_ = 0.0f;
        vy_ = 0.0f;
    }

    return active_;
}

void Inertia::stop() {
    active_ = false;
    vx_ = 0.0f;
    vy_ = 0.0f;
}

} // namespace windeck::modes
