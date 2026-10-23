#pragma once

#include "config/profile.h"
#include "neptune/neptune_device.h"
#include "modes/mouse_mode.h"
#include "modes/directional_mode.h"
#include "modes/inertia.h"
#include "modes/tick_timer.h"
#include "modes/radial_menu_mode.h"
#include "output/synthetic_input.h"
#include <cstdint>

namespace windeck::overlay { class RadialOverlay; }

namespace windeck::mapper {

// Maps Neptune touchpad to trackpad/mouse/wasd/arrows/dpad/scroll/disabled.
class TouchpadMapper {
public:
    // Hold sets for this pad's WASD/Arrows keys, its click action and its radial
    // menu. They update independently, so each has its own.
    TouchpadMapper(windeck::output::HoldSource directional,
                   windeck::output::HoldSource click,
                   windeck::output::HoldSource radial)
        : directional_(directional), radial_menu_(radial), click_source_(click) {}

    void set_name(const char* name) { name_ = name; }
    // Set reference to Neptune device for haptic feedback on click.
    void set_haptic_device(windeck::neptune::NeptuneDevice* device,
                           windeck::neptune::HapticSide side) {
        haptic_device_ = device;
        haptic_motor_ = side;
        haptic_side_ = (side == windeck::neptune::HapticSide::Left) ? 1 : 0;
    }
    void set_overlay(windeck::overlay::RadialOverlay* overlay) { overlay_ = overlay; }

    void apply_config(const config::TouchpadConfig& cfg);

    // Process touchpad input each frame.
    // pad_x, pad_y: absolute coordinates (-32768..32767), 0 when not touching
    // pressure: 0-32767 (per hid-steam kernel driver)
    // touching: is finger on pad (from btn touch bit)
    void update(int16_t pad_x, int16_t pad_y, uint16_t pressure,
                bool touching, windeck::output::SyntheticInput& synth);

    // Tick inertia even when no new HID report (call every frame).
    void tick_inertia(windeck::output::SyntheticInput& synth);

    void release_all(windeck::output::SyntheticInput& synth);

private:
    config::TouchpadMode mode_ = config::TouchpadMode::Disabled;
    config::TouchpadAccelerationPreset acceleration_ = config::TouchpadAccelerationPreset::Normal;
    config::ButtonAction click_action_{};
    uint16_t pressure_threshold_ = 6144;
    bool invert_x_ = false;
    bool invert_y_ = false;

    // Track previous state for delta calculation
    bool was_touching_ = false;
    int16_t prev_x_ = 0;
    int16_t prev_y_ = 0;

    // Velocity tracking for inertia
    float vel_x_ = 0.0f;
    float vel_y_ = 0.0f;

    // Inertia confidence accumulator (0..1). Increments when speed > threshold,
    // decrements when speed < threshold. Inertia only fires if > 0.5 at lift-off.
    float inertia_confidence_ = 0.0f;
    // Time base for touch sample step (used for frame-independent speed/filters).
    modes::TickTimer touch_timer_;

    // Click state
    bool click_held_ = false;

    // Scroll accumulator for sub-pixel deltas
    float scroll_accum_ = 0.0f;

    // Mouse edge-continue state.
    bool mouse_edge_active_ = false;
    float mouse_edge_entry_touch_x_ = 0.0f;
    float mouse_edge_entry_touch_y_ = 0.0f;
    float mouse_edge_prev_touch_x_ = 0.0f;
    float mouse_edge_prev_touch_y_ = 0.0f;
    float mouse_edge_entry_motion_x_ = 0.0f;
    float mouse_edge_entry_motion_y_ = 0.0f;
    float mouse_edge_speed_ = 0.0f;
    float mouse_edge_angle_accum_ = 0.0f;
    float mouse_edge_target_motion_x_ = 0.0f;
    float mouse_edge_target_motion_y_ = 0.0f;
    float mouse_edge_smooth_motion_x_ = 0.0f;
    float mouse_edge_smooth_motion_y_ = 0.0f;
    bool mouse_edge_requires_rearm_ = false;
    modes::TickTimer edge_timer_;

    modes::MouseMode mouse_;
    modes::DirectionalMode directional_;
    modes::Inertia inertia_;
    modes::RadialMenuMode  radial_menu_;
    windeck::output::HoldSource click_source_;
    std::array<config::ButtonAction, 8> radial_keys_{};
    windeck::overlay::RadialOverlay* overlay_ = nullptr;
    const char* name_ = "pad";
    windeck::neptune::NeptuneDevice* haptic_device_ = nullptr;
    windeck::neptune::HapticSide haptic_motor_ = windeck::neptune::HapticSide::Right;
    uint8_t haptic_side_ = 0;  // radial overlay side: 0=right, 1=left

    void click_haptic();
    void edge_entry_haptic();
    // While the click is held, its action is the click source's whole set.
    void set_click_held(bool held, windeck::output::SyntheticInput& synth);
    void reset_mouse_edge_state();
};

} // namespace windeck::mapper
