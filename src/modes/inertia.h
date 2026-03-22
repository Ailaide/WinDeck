#pragma once

namespace windeck::modes {

// Exponential decay for trackpad inertia.
// When finger lifts off, velocity decays smoothly to zero.
class Inertia {
public:
    void set_friction(float friction) { friction_ = friction; }
    void set_cutoff(float cutoff) { cutoff_ = cutoff; }

    // Call when finger is on the pad: set current velocity.
    void set_velocity(float vx, float vy);

    // Call when finger lifts off: begin inertia from last velocity.
    void begin();

    // Call every frame (~4ms): decay velocity, return current delta.
    // Returns true if still moving (above cutoff).
    bool tick(float& out_dx, float& out_dy);

    // Stop immediately.
    void stop();

    bool is_active() const { return active_; }

private:
    float vx_ = 0.0f;
    float vy_ = 0.0f;
    float friction_ = 0.95f;  // Per-tick multiplier (0.95 = gentle, 0.8 = heavy)
    float cutoff_ = 0.5f;     // Stop when velocity magnitude < cutoff
    bool active_ = false;
};

} // namespace windeck::modes
