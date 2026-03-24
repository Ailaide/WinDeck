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

constexpr float kPadNormScale = 1.0f / 32767.0f;
constexpr float kMouseEdgeThreshold = 0.92f;
constexpr float kMouseEdgeRearmThreshold = 2.0f * kMouseEdgeThreshold - 1.0f;
constexpr float kMouseEdgeHoldThreshold = kMouseEdgeRearmThreshold;
constexpr float kMouseEdgeStartSpeed = 1.0f;
constexpr float kMouseEdgeMotionSmoothingHalfLifeTicks = 6.0f;
constexpr float kReferenceTickSeconds = 1.0f / 250.0f;

float vec_len(float x, float y) {
    return std::sqrt(x * x + y * y);
}

bool normalize_vec(float x, float y, float& out_x, float& out_y) {
    float mag = vec_len(x, y);
    if (mag <= 0.0001f) {
        out_x = 0.0f;
        out_y = 0.0f;
        return false;
    }
    out_x = x / mag;
    out_y = y / mag;
    return true;
}

bool is_edge_enter_zone(float touch_x, float touch_y) {
    return std::fabs(touch_x) >= kMouseEdgeThreshold ||
           std::fabs(touch_y) >= kMouseEdgeThreshold;
}

bool is_edge_hold_zone(float touch_x, float touch_y) {
    return std::fabs(touch_x) >= kMouseEdgeHoldThreshold ||
           std::fabs(touch_y) >= kMouseEdgeHoldThreshold;
}

bool is_rearmed_from_edge(float touch_x, float touch_y) {
    return std::fabs(touch_x) <= kMouseEdgeRearmThreshold &&
           std::fabs(touch_y) <= kMouseEdgeRearmThreshold;
}

float signed_angle_delta(float prev_x, float prev_y, float cur_x, float cur_y) {
    float cross = prev_x * cur_y - prev_y * cur_x;
    float dot = prev_x * cur_x + prev_y * cur_y;
    return std::atan2(cross, dot);
}

void rotate_vec(float x, float y, float angle, float& out_x, float& out_y) {
    float c = std::cos(angle);
    float s = std::sin(angle);
    out_x = x * c - y * s;
    out_y = x * s + y * c;
}

float step_from_time(bool& has_last_tick, std::chrono::steady_clock::time_point& last_tick) {
    auto now = std::chrono::steady_clock::now();
    float step = 1.0f;
    if (has_last_tick) {
        float dt = std::chrono::duration<float>(now - last_tick).count();
        if (dt > 0.0f) {
            step = dt / kReferenceTickSeconds;
        }
    }
    last_tick = now;
    has_last_tick = true;
    return step;
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
    touch_sample_has_last_tick_ = false;
    reset_mouse_edge_state();
    mouse_edge_requires_rearm_ = false;

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
    constexpr float kConfidenceStep = 0.1f; // Per 250Hz reference tick.

    if (touching) {
        inertia_.stop();

        if (was_touching_) {
            float dx = static_cast<float>(pad_x - prev_x_);
            float dy = static_cast<float>(pad_y - prev_y_);
            float touch_step = step_from_time(touch_sample_has_last_tick_, touch_sample_last_tick_);

            float scaled_dx = dx * kTouchScale;
            float scaled_dy = -dy * kTouchScale; // Invert Y: pad up = cursor up (negative screen Y)

            switch (mode_) {
                case config::TouchpadMode::TrackpadInertia: {
                    float speed = std::sqrt(scaled_dx * scaled_dx + scaled_dy * scaled_dy) / touch_step;
                    float gain = accel_gain_for_speed(speed, acceleration_);
                    float effective_dx = scaled_dx * gain;
                    float effective_dy = scaled_dy * gain;
                    float effective_vx = effective_dx / touch_step;
                    float effective_vy = effective_dy / touch_step;
                    float smooth_alpha = 1.0f - std::pow(0.5f, touch_step);

                    // Track accelerated velocity to keep inertia start speed continuous.
                    vel_x_ += (effective_vx - vel_x_) * smooth_alpha;
                    vel_y_ += (effective_vy - vel_y_) * smooth_alpha;

                    float cur_speed = std::sqrt(vel_x_ * vel_x_ + vel_y_ * vel_y_);
                    float confidence_step = kConfidenceStep * touch_step;
                    if (cur_speed >= kMinInertiaSpeed) {
                        inertia_confidence_ += confidence_step;
                        if (inertia_confidence_ > 1.0f) inertia_confidence_ = 1.0f;
                    } else {
                        inertia_confidence_ -= confidence_step;
                        if (inertia_confidence_ < 0.0f) inertia_confidence_ = 0.0f;
                    }

                    mouse_.update_delta(effective_dx, effective_dy, synth);
                    break;
                }

                case config::TouchpadMode::Mouse: {
                    inertia_confidence_ = 0.0f;

                    float speed = std::sqrt(scaled_dx * scaled_dx + scaled_dy * scaled_dy) / touch_step;
                    float gain = accel_gain_for_speed(speed, acceleration_);
                    float effective_dx = scaled_dx * gain;
                    float effective_dy = scaled_dy * gain;
                    float effective_vx = effective_dx / touch_step;
                    float effective_vy = effective_dy / touch_step;
                    float smooth_alpha = 1.0f - std::pow(0.5f, touch_step);

                    vel_x_ += (effective_vx - vel_x_) * smooth_alpha;
                    vel_y_ += (effective_vy - vel_y_) * smooth_alpha;
                    float cur_speed = vec_len(vel_x_, vel_y_);

                    float touch_x = static_cast<float>(pad_x) * kPadNormScale;
                    float touch_y = -static_cast<float>(pad_y) * kPadNormScale;
                    bool near_edge_enter = is_edge_enter_zone(touch_x, touch_y);
                    bool near_edge_hold = is_edge_hold_zone(touch_x, touch_y);
                    if (mouse_edge_requires_rearm_ && is_rearmed_from_edge(touch_x, touch_y)) {
                        mouse_edge_requires_rearm_ = false;
                    }

                    float touch_dir_x = 0.0f;
                    float touch_dir_y = 0.0f;
                    bool has_touch_dir = normalize_vec(touch_x, touch_y, touch_dir_x, touch_dir_y);

                    if (mouse_edge_active_) {
                        if (!near_edge_hold || !has_touch_dir) {
                            reset_mouse_edge_state();
                            mouse_.update_delta(effective_dx, effective_dy, synth);
                        } else {
                            float dtheta = signed_angle_delta(
                                mouse_edge_prev_touch_x_, mouse_edge_prev_touch_y_,
                                touch_dir_x, touch_dir_y);
                            mouse_edge_angle_accum_ += dtheta;
                            // Normalize to [-pi, pi] to prevent float precision loss
                            if (mouse_edge_angle_accum_ > 3.14159265f)
                                mouse_edge_angle_accum_ -= 6.28318530f;
                            else if (mouse_edge_angle_accum_ < -3.14159265f)
                                mouse_edge_angle_accum_ += 6.28318530f;
                            mouse_edge_prev_touch_x_ = touch_dir_x;
                            mouse_edge_prev_touch_y_ = touch_dir_y;
                        }
                    } else {
                        mouse_.update_delta(effective_dx, effective_dy, synth);

                        if (!mouse_edge_requires_rearm_ &&
                            near_edge_enter && has_touch_dir && cur_speed >= kMouseEdgeStartSpeed) {
                            float motion_dir_x = 0.0f;
                            float motion_dir_y = 0.0f;
                            if (normalize_vec(vel_x_, vel_y_, motion_dir_x, motion_dir_y)) {
                                mouse_edge_active_ = true;
                                mouse_edge_entry_touch_x_ = touch_dir_x;
                                mouse_edge_entry_touch_y_ = touch_dir_y;
                                mouse_edge_prev_touch_x_ = touch_dir_x;
                                mouse_edge_prev_touch_y_ = touch_dir_y;
                                mouse_edge_entry_motion_x_ = motion_dir_x;
                                mouse_edge_entry_motion_y_ = motion_dir_y;
                                mouse_edge_speed_ = cur_speed;
                                mouse_edge_angle_accum_ = 0.0f;
                                mouse_edge_target_motion_x_ = motion_dir_x * mouse_edge_speed_;
                                mouse_edge_target_motion_y_ = motion_dir_y * mouse_edge_speed_;
                                mouse_edge_smooth_motion_x_ = mouse_edge_target_motion_x_;
                                mouse_edge_smooth_motion_y_ = mouse_edge_target_motion_y_;
                                mouse_edge_requires_rearm_ = true;
                                edge_entry_haptic();
                            }
                        }
                    }
                    break;
                }

                case config::TouchpadMode::Wasd:
                case config::TouchpadMode::Arrows: {
                    reset_mouse_edge_state();
                    vel_x_ = 0.0f;
                    vel_y_ = 0.0f;
                    inertia_confidence_ = 0.0f;
                    float fx = static_cast<float>(pad_x) / 32767.0f;
                    float fy = static_cast<float>(pad_y) / 32767.0f;
                    directional_.update_analog(fx, fy, 0.2f, synth);
                    break;
                }

                case config::TouchpadMode::Scroll:
                    reset_mouse_edge_state();
                    vel_x_ = 0.0f;
                    vel_y_ = 0.0f;
                    inertia_confidence_ = 0.0f;
                    scroll_accum_ += scaled_dy * 3.0f;
                    {
                        int scroll_int = static_cast<int>(scroll_accum_);
                        if (scroll_int != 0) {
                            synth.mouse_scroll(scroll_int);
                            scroll_accum_ -= static_cast<float>(scroll_int);
                        }
                    }
                    break;

                default:
                    reset_mouse_edge_state();
                    vel_x_ = 0.0f;
                    vel_y_ = 0.0f;
                    inertia_confidence_ = 0.0f;
                    break;
            }
        } else {
            touch_sample_has_last_tick_ = false;
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
            if (mode_ == config::TouchpadMode::Mouse) {
                reset_mouse_edge_state();
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
        touch_sample_has_last_tick_ = false;
    }
}

void TouchpadMapper::tick_inertia(windeck::output::SyntheticInput& synth) {
    if (mode_ == config::TouchpadMode::Mouse && mouse_edge_active_) {
        float step = step_from_time(mouse_edge_has_last_tick_, mouse_edge_last_tick_);
        float steer_dir_x = 0.0f;
        float steer_dir_y = 0.0f;
        rotate_vec(mouse_edge_entry_motion_x_, mouse_edge_entry_motion_y_,
                   mouse_edge_angle_accum_, steer_dir_x, steer_dir_y);

        mouse_edge_target_motion_x_ = steer_dir_x * mouse_edge_speed_;
        mouse_edge_target_motion_y_ = steer_dir_y * mouse_edge_speed_;

        float smooth_alpha = 1.0f - std::pow(0.5f, step / kMouseEdgeMotionSmoothingHalfLifeTicks);
        mouse_edge_smooth_motion_x_ += (mouse_edge_target_motion_x_ - mouse_edge_smooth_motion_x_) * smooth_alpha;
        mouse_edge_smooth_motion_y_ += (mouse_edge_target_motion_y_ - mouse_edge_smooth_motion_y_) * smooth_alpha;

        mouse_.update_delta(mouse_edge_smooth_motion_x_ * step,
                            mouse_edge_smooth_motion_y_ * step, synth);
    }

    if (!inertia_.is_active()) return;

    float dx, dy;
    if (inertia_.tick(dx, dy)) {
        mouse_.update_delta(dx, dy, synth);
    }
}

void TouchpadMapper::release_all(windeck::output::SyntheticInput& synth) {
    directional_.release_all(synth);
    inertia_.stop();
    inertia_confidence_ = 0.0f;
    touch_sample_has_last_tick_ = false;
    reset_mouse_edge_state();
    mouse_edge_requires_rearm_ = false;
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

void TouchpadMapper::edge_entry_haptic() {
    if (!haptic_device_) return;
    // Same 0xEA click haptic as touchpad click, but about one-third weaker.
    // 2/3 amplitude is ~ -3.5 dB; use -4 dB as nearest integer.
    uint8_t side = (haptic_side_ == 0) ? 2 : 1;
    haptic_device_->send_haptic_cmd(side, 2, -4);
}

void TouchpadMapper::reset_mouse_edge_state() {
    mouse_edge_active_ = false;
    mouse_edge_entry_touch_x_ = 0.0f;
    mouse_edge_entry_touch_y_ = 0.0f;
    mouse_edge_prev_touch_x_ = 0.0f;
    mouse_edge_prev_touch_y_ = 0.0f;
    mouse_edge_entry_motion_x_ = 0.0f;
    mouse_edge_entry_motion_y_ = 0.0f;
    mouse_edge_speed_ = 0.0f;
    mouse_edge_angle_accum_ = 0.0f;
    mouse_edge_target_motion_x_ = 0.0f;
    mouse_edge_target_motion_y_ = 0.0f;
    mouse_edge_smooth_motion_x_ = 0.0f;
    mouse_edge_smooth_motion_y_ = 0.0f;
    mouse_edge_has_last_tick_ = false;
}

} // namespace windeck::mapper
