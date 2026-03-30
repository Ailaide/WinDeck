#include "mapper/stick_mapper.h"
#include "mapper/dead_zone.h"

namespace windeck::mapper {

void StickMapper::apply_config(const config::StickConfig& cfg) {
    mode_ = cfg.mode;
    inner_ = cfg.inner_deadzone;
    outer_ = cfg.outer_deadzone;
    curve_ = cfg.curve;
    invert_x_ = cfg.invert_x;
    invert_y_ = cfg.invert_y;
    // Re-arm stick mouse timebase on config/profile changes to avoid first-frame spikes.
    mouse_.reset_timebase();

    switch (cfg.mode) {
        case config::StickMode::Wasd:
            directional_.set_keys_wasd();
            directional_.set_overlap(cfg.overlap);
            break;
        case config::StickMode::Arrows:
            directional_.set_keys_arrows();
            directional_.set_overlap(cfg.overlap);
            break;
        case config::StickMode::Mouse:
            mouse_.set_sensitivity(cfg.mouse_sensitivity);
            break;
        default:
            break;
    }
}

void StickMapper::map(int16_t raw_x, int16_t raw_y,
                       int16_t& out_x, int16_t& out_y,
                       windeck::output::SyntheticInput& synth) {
    int16_t dz_x, dz_y;
    apply_stick_deadzone(raw_x, raw_y, dz_x, dz_y, inner_, outer_, curve_);
    if (invert_x_) dz_x = static_cast<int16_t>(-dz_x);
    if (invert_y_) dz_y = static_cast<int16_t>(-dz_y);

    switch (mode_) {
        case config::StickMode::Stick:
            out_x = dz_x;
            out_y = dz_y;
            break;

        case config::StickMode::Wasd:
        case config::StickMode::Arrows: {
            out_x = 0;
            out_y = 0;
            float fx = static_cast<float>(dz_x) / 32767.0f;
            float fy = static_cast<float>(dz_y) / 32767.0f;
            directional_.update_analog(fx, fy, 0.1f, synth);
            break;
        }

        case config::StickMode::Mouse: {
            out_x = 0;
            out_y = 0;
            float fx = static_cast<float>(dz_x) / 32767.0f;
            float fy = static_cast<float>(dz_y) / 32767.0f;
            mouse_.update_stick(fx, fy, synth);
            break;
        }
    }
}

void StickMapper::release_all(windeck::output::SyntheticInput& synth) {
    directional_.release_all(synth);
}

} // namespace windeck::mapper
