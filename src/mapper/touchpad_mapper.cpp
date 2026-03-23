#include "mapper/touchpad_mapper.h"
#include <cmath>

namespace windeck::mapper {

namespace {

struct AccelParams {
    float start_speed;
    float strength;
    float exponent;
    float max_gain;
};

AccelParams get_accel_params(config::TouchpadAccelerationPreset preset) {
    switch (preset) {
        case config::TouchpadAccelerationPreset::Strong:
            return {0.6f, 0.85f, 1.45f, 3.0f};
        case config::TouchpadAccelerationPreset::Off:
            return {0.0f, 0.0f, 1.0f, 1.0f};
        case config::TouchpadAccelerationPreset::Normal:
        default:
            return {0.8f, 0.55f, 1.35f, 2.2f};
    }
}

float accel_gain_for_speed(float speed, config::TouchpadAccelerationPreset preset) {
    if (preset == config::TouchpadAccelerationPreset::Off) {
        return 1.0f;
    }

    const AccelParams p = get_accel_params(preset);
    if (speed <= p.start_speed) {
        return 1.0f;
    }

    float gain = 1.0f + p.strength * std::pow(speed - p.start_speed, p.exponent);
    if (gain > p.max_gain) {
        gain = p.max_gain;
    }
    return gain;
}

}  // namespace

void TouchpadMapper::apply_config(const config::TouchpadConfig& cfg) {
    mode_ = cfg.mode;
    acceleration_ = cfg.acceleration;
    pressure_threshold_ = cfg.pressure_threshold;
    click_action_ = cfg.click_action;
    mouse_.set_sensitivity(cfg.sensitivity);
    // Friction is configured as "feel" value (0.95 = gentle).
    // Convert to per-frame at 250Hz: pow(feel, 1) is too fast,
    // so we remap to a higher value for smooth decay.
    // 0.95 feel → 0.985 per frame → ~0.16s decay at 250Hz
    float per_frame_friction = 1.0f - (1.0f - cfg.friction) * 0.3f;
    inertia_.set_friction(per_frame_friction);
    inertia_.set_cutoff(0.1f);

    switch (mode_) {
        case config::TouchpadMode::Wasd:   directional_.set_keys_wasd(); break;
        case config::TouchpadMode::Arrows: directional_.set_keys_arrows(); break;
        default: break;
    }
}

void TouchpadMapper::update(int16_t pad_x, int16_t pad_y, uint16_t pressure,
                              bool touching, windeck::output::SyntheticInput& synth) {
    if (mode_ == config::TouchpadMode::Disabled) return;

    // --- Click with hysteresis (pressure jitters) ---
    if (mode_ == config::TouchpadMode::TrackpadInertia ||
        mode_ == config::TouchpadMode::Mouse) {
        bool click_now;
        if (click_held_) {
            click_now = touching && (pressure >= pressure_threshold_ * 2 / 3);
        } else {
            click_now = touching && (pressure >= pressure_threshold_);
        }

        if (click_now && !click_held_) {
            emit_action(click_action_, true, synth);
            click_haptic();
        } else if (!click_now && click_held_) {
            emit_action(click_action_, false, synth);
            click_haptic();
        }
        click_held_ = click_now;
    }

    // --- Touch tracking ---
    constexpr float kTouchScale = 0.0065f;
    constexpr float kMinInertiaSpeed = 3.0f;
    constexpr float kConfidenceStep = 0.1f; // ~10 frames to go from 0 to 1

    if (touching) {
        inertia_.stop();

        if (was_touching_) {
            float dx = static_cast<float>(pad_x - prev_x_);
            float dy = static_cast<float>(pad_y - prev_y_);

            float scaled_dx = dx * kTouchScale;
            float scaled_dy = -dy * kTouchScale; // Invert Y: pad up = cursor up (negative screen Y)

            switch (mode_) {
                case config::TouchpadMode::TrackpadInertia:
                case config::TouchpadMode::Mouse: {
                    float speed = std::sqrt(scaled_dx * scaled_dx + scaled_dy * scaled_dy);
                    float gain = accel_gain_for_speed(speed, acceleration_);
                    float effective_dx = scaled_dx * gain;
                    float effective_dy = scaled_dy * gain;

                    // Track accelerated velocity to keep inertia start speed continuous.
                    vel_x_ = vel_x_ * 0.5f + effective_dx * 0.5f;
                    vel_y_ = vel_y_ * 0.5f + effective_dy * 0.5f;

                    float cur_speed = std::sqrt(vel_x_ * vel_x_ + vel_y_ * vel_y_);
                    if (cur_speed >= kMinInertiaSpeed) {
                        inertia_confidence_ += kConfidenceStep;
                        if (inertia_confidence_ > 1.0f) inertia_confidence_ = 1.0f;
                    } else {
                        inertia_confidence_ -= kConfidenceStep;
                        if (inertia_confidence_ < 0.0f) inertia_confidence_ = 0.0f;
                    }

                    mouse_.update_delta(effective_dx, effective_dy, synth);
                    break;
                }

                case config::TouchpadMode::Wasd:
                case config::TouchpadMode::Arrows: {
                    vel_x_ = 0.0f;
                    vel_y_ = 0.0f;
                    inertia_confidence_ = 0.0f;
                    float fx = static_cast<float>(pad_x) / 32767.0f;
                    float fy = static_cast<float>(pad_y) / 32767.0f;
                    directional_.update_analog(fx, fy, 0.2f, synth);
                    break;
                }

                case config::TouchpadMode::Scroll:
                    vel_x_ = 0.0f;
                    vel_y_ = 0.0f;
                    inertia_confidence_ = 0.0f;
                    synth.mouse_scroll(static_cast<int>(scaled_dy * 3.0f));
                    break;

                default:
                    vel_x_ = 0.0f;
                    vel_y_ = 0.0f;
                    inertia_confidence_ = 0.0f;
                    break;
            }
        }

        prev_x_ = pad_x;
        prev_y_ = pad_y;
        was_touching_ = true;
    } else {
        if (was_touching_) {
            // Finger just lifted — start inertia only if confidence > 0.5
            if (mode_ == config::TouchpadMode::TrackpadInertia) {
                if (inertia_confidence_ > 0.5f) {
                    inertia_.set_velocity(vel_x_, vel_y_);
                    inertia_.begin();
                }
                inertia_confidence_ = 0.0f;
            }

            if (mode_ == config::TouchpadMode::Wasd ||
                mode_ == config::TouchpadMode::Arrows) {
                directional_.release_all(synth);
            }
        }

        was_touching_ = false;
        vel_x_ = 0.0f;
        vel_y_ = 0.0f;
        inertia_confidence_ = 0.0f;
    }
}

void TouchpadMapper::tick_inertia(windeck::output::SyntheticInput& synth) {
    if (!inertia_.is_active()) return;

    float dx, dy;
    if (inertia_.tick(dx, dy)) {
        mouse_.update_delta(dx, dy, synth);
    }
}

void TouchpadMapper::release_all(windeck::output::SyntheticInput& synth) {
    directional_.release_all(synth);
    inertia_.stop();
    if (click_held_) {
        emit_action(click_action_, false, synth);
        click_held_ = false;
    }
}

void TouchpadMapper::emit_action(const config::ButtonAction& action, bool pressed,
                                   windeck::output::SyntheticInput& synth) {
    switch (action.type) {
        case config::ActionType::MouseButton:
            synth.mouse_set(static_cast<uint8_t>(action.code), pressed);
            break;
        case config::ActionType::Keyboard:
            synth.key_set(action.code, pressed);
            break;
        default:
            break;
    }
}

void TouchpadMapper::click_haptic() {
    if (!haptic_device_) return;
    // 0xEA cmd=2 (click), side: 1=left 2=right, gain=4dB for stronger feel
    uint8_t side = (haptic_side_ == 0) ? 2 : 1;
    haptic_device_->send_haptic_cmd(side, 2, 0);
}

} // namespace windeck::mapper
