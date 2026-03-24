#include "mapper/gyro_mapper.h"
#include <algorithm>

namespace windeck::mapper {

namespace {
constexpr float kReferenceTickSeconds = 1.0f / 250.0f;
}

void GyroMapper::apply_config(const config::GyroConfig& cfg) {
    mode_ = cfg.mode;
    activation_ = cfg.activation;
    sensitivity_x_ = cfg.sensitivity_x;
    sensitivity_y_ = cfg.sensitivity_y;
    has_last_update_ = false;
    mouse_.set_sensitivity(1.0f); // Sensitivity handled here, not in MouseMode
}

bool GyroMapper::is_active(bool touching_left, bool touching_right) const {
    switch (activation_) {
        case config::GyroActivation::TouchRightPad: return touching_right;
        case config::GyroActivation::TouchLeftPad:  return touching_left;
        case config::GyroActivation::Always:        return true;
        default:                                    return true;
    }
}

void GyroMapper::update(int16_t gyro_pitch, int16_t gyro_yaw, int16_t /*gyro_roll*/,
                        bool touching_left_pad, bool touching_right_pad,
                        int16_t& out_rx, int16_t& out_ry,
                        windeck::output::SyntheticInput& synth) {
    if (mode_ == config::GyroMode::Off) {
        has_last_update_ = false;
        return;
    }
    if (!is_active(touching_left_pad, touching_right_pad)) {
        has_last_update_ = false;
        return;
    }

    auto now = std::chrono::steady_clock::now();
    float step = 1.0f;
    if (has_last_update_) {
        float dt = std::chrono::duration<float>(now - last_update_).count();
        if (dt > 0.0f) {
            step = dt / kReferenceTickSeconds;
        }
    }
    last_update_ = now;
    has_last_update_ = true;

    // Neptune gyro: raw +/-32768 range, ~2000 dps full scale
    // Yaw = horizontal rotation (left/right aim), Pitch = vertical (up/down aim)
    float yaw = static_cast<float>(gyro_yaw) / 32767.0f;
    float pitch = static_cast<float>(gyro_pitch) / 32767.0f;

    // Apply per-axis sensitivity
    float dx = yaw * sensitivity_x_;
    float dy = -pitch * sensitivity_y_; // Invert: pitch up = aim up

    switch (mode_) {
        case config::GyroMode::RightStick: {
            // Scale to stick range and clamp
            constexpr float kGyroStickScale = 8000.0f;
            int32_t rx = static_cast<int32_t>(dx * kGyroStickScale);
            int32_t ry = static_cast<int32_t>(dy * kGyroStickScale);
            out_rx = static_cast<int16_t>(std::clamp(rx, static_cast<int32_t>(-32768), static_cast<int32_t>(32767)));
            out_ry = static_cast<int16_t>(std::clamp(ry, static_cast<int32_t>(-32768), static_cast<int32_t>(32767)));
            break;
        }
        case config::GyroMode::Mouse: {
            constexpr float kGyroMouseScale = 20.0f;
            mouse_.update_delta(dx * kGyroMouseScale * step,
                                dy * kGyroMouseScale * step, synth);
            break;
        }
        default:
            break;
    }
}

} // namespace windeck::mapper
