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
    timer_.reset();
}

bool Inertia::tick(float& out_dx, float& out_dy) {
    if (!active_) {
        out_dx = 0.0f;
        out_dy = 0.0f;
        return false;
    }

    float step = timer_.step();

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
        timer_.reset();
    }

    return active_;
}

void Inertia::stop() {
    active_ = false;
    vx_ = 0.0f;
    vy_ = 0.0f;
    timer_.reset();
}

} // namespace windeck::modes
