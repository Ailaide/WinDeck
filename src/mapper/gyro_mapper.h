#pragma once

#include "config/profile.h"
#include "modes/mouse_mode.h"
#include "output/synthetic_input.h"
#include <cstdint>

namespace windeck::mapper {

// Maps Neptune gyroscope to right stick offset or mouse movement.
class GyroMapper {
public:
    void apply_config(const config::GyroConfig& cfg);

    // Process gyro data each frame.
    // gyro_pitch/yaw/roll: raw int16 from Neptune (~±2000 dps range)
    // touching_left/right_pad: capacitive touch state for activation
    // out_rx/ry: additive offset to right stick (only in right_stick mode)
    void update(int16_t gyro_pitch, int16_t gyro_yaw, int16_t gyro_roll,
                bool touching_left_pad, bool touching_right_pad,
                int16_t& out_rx, int16_t& out_ry,
                windeck::output::SyntheticInput& synth);

private:
    config::GyroMode mode_ = config::GyroMode::Off;
    config::GyroActivation activation_ = config::GyroActivation::Always;
    float sensitivity_x_ = 1.0f;
    float sensitivity_y_ = 1.0f;

    modes::MouseMode mouse_;

    bool is_active(bool touching_left, bool touching_right) const;
};

} // namespace windeck::mapper
