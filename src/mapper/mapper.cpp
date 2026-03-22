#include "mapper/mapper.h"
#include "mapper/dead_zone.h"
#include "neptune/neptune_report.h"
#include <cstring>
#include <algorithm>

namespace windeck::mapper {

void Mapper::set_device(windeck::neptune::NeptuneDevice* device) {
    left_touchpad_.set_haptic_device(device, 1);   // left grip
    right_touchpad_.set_haptic_device(device, 0);  // right grip
}

void Mapper::apply_profile(const config::Profile& profile) {
    button_mapper_.set_config(profile.buttons);

    left_stick_.apply_config(profile.left_stick);
    right_stick_.apply_config(profile.right_stick);

    left_trigger_.set_threshold(profile.left_trigger.threshold);
    right_trigger_.set_threshold(profile.right_trigger.threshold);

    dpad_.apply_config(profile.dpad);

    left_touchpad_.set_name("L-Pad");
    left_touchpad_.apply_config(profile.left_touchpad);
    right_touchpad_.set_name("R-Pad");
    right_touchpad_.apply_config(profile.right_touchpad);

    gyro_.apply_config(profile.gyro);
}

MapResult Mapper::map(const neptune::InputReport& input,
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
    result.xbox_report.bLeftTrigger  = left_trigger_.map(input.left_trigger);
    result.xbox_report.bRightTrigger = right_trigger_.map(input.right_trigger);

    // Touchpads — coordinates reset to (0,0) when finger lifts (verified on hardware)
    bool touching_left  = (input.left_pad_x != 0 || input.left_pad_y != 0);
    bool touching_right = (input.right_pad_x != 0 || input.right_pad_y != 0);

    left_touchpad_.update(input.left_pad_x, input.left_pad_y,
                           input.left_pad_pressure, touching_left, synth);
    right_touchpad_.update(input.right_pad_x, input.right_pad_y,
                            input.right_pad_pressure, touching_right, synth);

    // Gyro
    int16_t gyro_rx = 0, gyro_ry = 0;
    gyro_.update(input.gyro_pitch, input.gyro_yaw, input.gyro_roll,
                  touching_left, touching_right, gyro_rx, gyro_ry, synth);

    // Sum gyro offset into right stick
    if (gyro_rx != 0 || gyro_ry != 0) {
        int32_t rx = static_cast<int32_t>(result.xbox_report.sThumbRX) + gyro_rx;
        int32_t ry = static_cast<int32_t>(result.xbox_report.sThumbRY) + gyro_ry;
        result.xbox_report.sThumbRX = static_cast<SHORT>(std::clamp(rx, (int32_t)-32768, (int32_t)32767));
        result.xbox_report.sThumbRY = static_cast<SHORT>(std::clamp(ry, (int32_t)-32768, (int32_t)32767));
    }

    return result;
}

void Mapper::tick(windeck::output::SyntheticInput& synth) {
    left_touchpad_.tick_inertia(synth);
    right_touchpad_.tick_inertia(synth);
}

void Mapper::release_all(windeck::output::SyntheticInput& synth) {
    left_stick_.release_all(synth);
    right_stick_.release_all(synth);
    dpad_.release_all(synth);
    left_touchpad_.release_all(synth);
    right_touchpad_.release_all(synth);
}

} // namespace windeck::mapper
