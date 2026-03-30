#include "mapper/gyro_mapper.h"

namespace windeck::mapper {

void GyroMapper::apply_config(const config::GyroConfig& cfg) {
    mode_ = cfg.mode;
    sensitivity_x_ = cfg.sensitivity_x;
    sensitivity_y_ = cfg.sensitivity_y;
    invert_x_ = cfg.invert_x;
    invert_y_ = cfg.invert_y;
    timer_.reset();
    mouse_.set_sensitivity(1.0f);
}

void GyroMapper::update(int16_t gyro_pitch, int16_t gyro_yaw, int16_t /*gyro_roll*/,
                        windeck::output::SyntheticInput& synth) {
    if (mode_ == config::GyroMode::Disabled) {
        timer_.reset();
        return;
    }

    float step = timer_.step();

    float yaw = static_cast<float>(gyro_yaw) / 32767.0f;
    float pitch = static_cast<float>(gyro_pitch) / 32767.0f;

    float dx = yaw * sensitivity_x_ * (invert_x_ ? -1.0f : 1.0f);
    float dy = pitch * sensitivity_y_ * (invert_y_ ? -1.0f : 1.0f);

    constexpr float kGyroMouseScale = 40.0f;
    mouse_.update_delta(dx * kGyroMouseScale * step,
                        dy * kGyroMouseScale * step, synth);
}

} // namespace windeck::mapper
