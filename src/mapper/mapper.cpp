#include "mapper/mapper.h"
#include "overlay/radial_overlay.h"
#include "mapper/dead_zone.h"
#include "neptune/neptune_report.h"
#include <cstring>

namespace windeck::mapper {

void Mapper::set_overlay(windeck::overlay::RadialOverlay* overlay) {
    std::unique_lock lock(mutex_);
    left_touchpad_.set_overlay(overlay);
    right_touchpad_.set_overlay(overlay);
}

void Mapper::set_device(windeck::neptune::NeptuneDevice* device) {
    left_touchpad_.set_haptic_device(device, 1);   // left grip
    right_touchpad_.set_haptic_device(device, 0);  // right grip
}

void Mapper::apply_profile(const config::Profile& profile,
                           windeck::output::SyntheticInput& synth) {
    std::unique_lock lock(mutex_);
    release_all_unlocked(synth);
    synth.flush();
    button_mapper_.set_config(profile.buttons);

    left_stick_.apply_config(profile.left_stick);
    right_stick_.apply_config(profile.right_stick);

    left_trigger_.apply_config(profile.left_trigger);
    right_trigger_.apply_config(profile.right_trigger);

    dpad_.apply_config(profile.dpad);

    left_touchpad_.set_name("L-Pad");
    left_touchpad_.apply_config(profile.left_touchpad);
    right_touchpad_.set_name("R-Pad");
    right_touchpad_.apply_config(profile.right_touchpad);

    gyro_.apply_config(profile.gyro);
}

MapResult Mapper::map(const neptune::InputReport& input,
                       windeck::output::SyntheticInput& synth) {
    std::shared_lock lock(mutex_);
    return map_unlocked(input, synth);
}

MapResult Mapper::map_unlocked(const neptune::InputReport& input,
                                windeck::output::SyntheticInput& synth) {
    MapResult result;
    std::memset(&result.xbox_report, 0, sizeof(result.xbox_report));

    // Buttons
    result.xbox_report.wButtons = button_mapper_.map(
        input.buttons, input.buttons_ext, result.sys_events, synth);

    // D-Pad
    result.xbox_report.wButtons |= dpad_.map(input.buttons, synth);

    // Sticks
    left_stick_.map(input.left_stick_x, input.left_stick_y,
                     result.xbox_report.sThumbLX, result.xbox_report.sThumbLY, synth);
    right_stick_.map(input.right_stick_x, input.right_stick_y,
                      result.xbox_report.sThumbRX, result.xbox_report.sThumbRY, synth);

    // Triggers
    result.xbox_report.bLeftTrigger  = left_trigger_.map(input.left_trigger, synth);
    result.xbox_report.bRightTrigger = right_trigger_.map(input.right_trigger, synth);

    // Touchpads — coordinates reset to (0,0) when finger lifts (verified on hardware)
    bool touching_left  = (input.left_pad_x != 0 || input.left_pad_y != 0);
    bool touching_right = (input.right_pad_x != 0 || input.right_pad_y != 0);

    left_touchpad_.update(input.left_pad_x, input.left_pad_y,
                           input.left_pad_pressure, touching_left, synth);
    right_touchpad_.update(input.right_pad_x, input.right_pad_y,
                            input.right_pad_pressure, touching_right, synth);

    // Gyro
    gyro_.update(input.gyro_pitch, input.gyro_yaw, input.gyro_roll, synth);

    return result;
}

MapResult Mapper::map_and_flush(const neptune::InputReport& input,
                                windeck::output::SyntheticInput& synth) {
    std::shared_lock lock(mutex_);
    MapResult result = map_unlocked(input, synth);
    left_touchpad_.tick_inertia(synth);
    right_touchpad_.tick_inertia(synth);
    synth.reconcile_holds();   // resolve WASD/Arrows holds across all sources
    synth.flush();
    return result;
}

void Mapper::tick(windeck::output::SyntheticInput& synth) {
    std::shared_lock lock(mutex_);
    left_touchpad_.tick_inertia(synth);
    right_touchpad_.tick_inertia(synth);
}

void Mapper::tick_and_flush(windeck::output::SyntheticInput& synth) {
    std::shared_lock lock(mutex_);
    left_touchpad_.tick_inertia(synth);
    right_touchpad_.tick_inertia(synth);
    synth.flush();
}

void Mapper::release_all(windeck::output::SyntheticInput& synth) {
    std::unique_lock lock(mutex_);
    release_all_unlocked(synth);
}

void Mapper::release_all_unlocked(windeck::output::SyntheticInput& synth) {
    left_stick_.release_all(synth);
    right_stick_.release_all(synth);
    dpad_.release_all(synth);
    left_touchpad_.release_all(synth);
    right_touchpad_.release_all(synth);
    left_trigger_.release_all(synth);
    right_trigger_.release_all(synth);
}

} // namespace windeck::mapper
