#pragma once

#include "config/profile.h"
#include "modes/mouse_mode.h"
#include "modes/tick_timer.h"
#include "output/synthetic_input.h"
#include <cstdint>

namespace windeck::mapper {

// Maps Neptune gyroscope to mouse movement.
class GyroMapper {
public:
    void apply_config(const config::GyroConfig& cfg);

    // Process gyro data each frame.
    void update(int16_t gyro_pitch, int16_t gyro_yaw, int16_t gyro_roll,
                windeck::output::SyntheticInput& synth);

private:
    config::GyroMode mode_ = config::GyroMode::Disabled;
    float sensitivity_x_ = 1.0f;
    float sensitivity_y_ = 1.0f;
    bool invert_x_ = false;
    bool invert_y_ = false;
    modes::TickTimer timer_;
    modes::MouseMode mouse_;
};

} // namespace windeck::mapper
