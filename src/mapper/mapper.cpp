#include "mapper/mapper.h"
#include <cstring>

namespace windeck::mapper {

MapResult Mapper::map(const neptune::InputReport& input) {
    MapResult result;
    std::memset(&result.xbox_report, 0, sizeof(result.xbox_report));

    // Buttons (system buttons intercepted here)
    result.xbox_report.wButtons = button_mapper_.map(input.buttons, input.buttons_ext, result.sys_events);

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
