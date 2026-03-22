#include "mapper/touchpad_mapper.h"
#include <spdlog/spdlog.h>
#include <cmath>

namespace windeck::mapper {

void TouchpadMapper::apply_config(const config::TouchpadConfig& cfg) {
    mode_ = cfg.mode;
    pressure_threshold_ = cfg.pressure_threshold;
    click_action_ = cfg.click_action;
    mouse_.set_sensitivity(cfg.sensitivity);
    inertia_.set_friction(cfg.friction);
    inertia_.set_cutoff(0.5f);

    switch (mode_) {
        case config::TouchpadMode::Wasd:   directional_.set_keys_wasd(); break;
        case config::TouchpadMode::Arrows: directional_.set_keys_arrows(); break;
        default: break;
    }
}

void TouchpadMapper::update(int16_t pad_x, int16_t pad_y, uint16_t pressure,
                              bool touching, windeck::output::SyntheticInput& synth) {
    if (mode_ == config::TouchpadMode::Disabled) return;


    // Handle click with hysteresis (pressure jitters significantly).
    // Press at threshold, release at threshold/2.
    bool click_pressed;
    if (click_held_) {
        click_pressed = touching && (pressure >= pressure_threshold_ / 2);
    } else {
        click_pressed = touching && (pressure >= pressure_threshold_);
    }
    if (mode_ == config::TouchpadMode::TrackpadInertia ||
        mode_ == config::TouchpadMode::Mouse) {
        if (click_pressed && !click_held_) {
            if (click_action_.type == config::ActionType::MouseButton)
                synth.mouse_press(static_cast<uint8_t>(click_action_.code));
            else if (click_action_.type == config::ActionType::Keyboard)
                synth.key_press(click_action_.code);
            click_haptic();
        } else if (!click_pressed && click_held_) {
            if (click_action_.type == config::ActionType::MouseButton)
                synth.mouse_release(static_cast<uint8_t>(click_action_.code));
            else if (click_action_.type == config::ActionType::Keyboard)
                synth.key_release(click_action_.code);
            click_haptic();
        }
        click_held_ = click_pressed;
    }

    if (touching) {
        inertia_.stop();

        // Diagnostic (first 30 frames per pad)
        static int diag_l = 0, diag_r = 0;
        int& diag = (name_[0] == 'R') ? diag_r : diag_l;
        if (diag < 30) {
            spdlog::info("{}: x={} y={} press={} was={} mode={}",
                name_, pad_x, pad_y, pressure, was_touching_, static_cast<int>(mode_));
            diag++;
        }

        if (was_touching_) {
            // Calculate delta from previous position
            float dx = static_cast<float>(pad_x - prev_x_) * 0.01f; // Scale to reasonable mouse units
            float dy = static_cast<float>(pad_y - prev_y_) * -0.01f; // Invert Y for screen coords

            vel_x_ = dx;
            vel_y_ = dy;

            switch (mode_) {
                case config::TouchpadMode::TrackpadInertia:
                case config::TouchpadMode::Mouse:
                    mouse_.update_delta(dx, dy, synth);
                    break;

                case config::TouchpadMode::Wasd:
                case config::TouchpadMode::Arrows: {
                    // Use absolute position as analog direction
                    float fx = static_cast<float>(pad_x) / 32767.0f;
                    float fy = static_cast<float>(pad_y) / 32767.0f;
                    directional_.update_analog(fx, fy, 0.2f, synth);
                    break;
                }

                case config::TouchpadMode::Scroll:
                    synth.mouse_scroll(static_cast<int>(dy * 10.0f));
                    break;

                case config::TouchpadMode::DpadButtons:
                    // Map touchpad quadrants to D-Pad
                    // Handled by ButtonMapper via touchpad click actions
                    break;

                default:
                    break;
            }
        }

        prev_x_ = pad_x;
        prev_y_ = pad_y;
        was_touching_ = true;
    } else {
        if (was_touching_) {
            // Finger just lifted
            if (mode_ == config::TouchpadMode::TrackpadInertia) {
                inertia_.set_velocity(vel_x_, vel_y_);
                inertia_.begin();
            }

            if (mode_ == config::TouchpadMode::Wasd ||
                mode_ == config::TouchpadMode::Arrows) {
                directional_.release_all(synth);
            }
        }

        was_touching_ = false;
        vel_x_ = 0.0f;
        vel_y_ = 0.0f;
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
        if (click_action_.type == config::ActionType::MouseButton)
            synth.mouse_release(static_cast<uint8_t>(click_action_.code));
        else if (click_action_.type == config::ActionType::Keyboard)
            synth.key_release(click_action_.code);
        click_held_ = false;
    }
}

void TouchpadMapper::click_haptic() {
    if (!haptic_device_) return;
    // Short tick: low amplitude, short duration
    haptic_device_->send_haptic(haptic_side_, 5000, 2000, 1);
}

} // namespace windeck::mapper
