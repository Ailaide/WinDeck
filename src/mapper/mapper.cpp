#include "mapper/mapper.h"
#include "mapper/dead_zone.h"
#include <cstring>

namespace windeck::mapper {

void Mapper::apply_profile(const config::Profile& profile) {
    // Buttons
    button_mapper_.set_config(profile.buttons);

    // Sticks
    left_stick_.set_deadzone(profile.left_stick.inner_deadzone,
                              profile.left_stick.outer_deadzone);
    left_stick_.set_response_curve(profile.left_stick.curve);

    right_stick_.set_deadzone(profile.right_stick.inner_deadzone,
                               profile.right_stick.outer_deadzone);
    right_stick_.set_response_curve(profile.right_stick.curve);

    // Triggers
    left_trigger_.set_threshold(profile.left_trigger.threshold);
    right_trigger_.set_threshold(profile.right_trigger.threshold);
}

MapResult Mapper::map(const neptune::InputReport& input,
                       windeck::output::SyntheticInput& synth) {
    MapResult result;
    std::memset(&result.xbox_report, 0, sizeof(result.xbox_report));

    // Buttons (with remapping + keyboard/mouse output)
    result.xbox_report.wButtons = button_mapper_.map(
        input.buttons, input.buttons_ext, result.sys_events, synth);

    // Sticks
    left_stick_.map(input.left_stick_x, input.left_stick_y,
                     result.xbox_report.sThumbLX, result.xbox_report.sThumbLY);
    right_stick_.map(input.right_stick_x, input.right_stick_y,
                      result.xbox_report.sThumbRX, result.xbox_report.sThumbRY);

    // Triggers
    result.xbox_report.bLeftTrigger  = left_trigger_.map(input.left_trigger);
    result.xbox_report.bRightTrigger = right_trigger_.map(input.right_trigger);

    return result;
}

} // namespace windeck::mapper
