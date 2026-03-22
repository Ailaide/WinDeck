#include "mapper/dpad_mapper.h"
#include "neptune/neptune_report.h"

#ifndef XUSB_GAMEPAD_DPAD_UP
#define XUSB_GAMEPAD_DPAD_UP        0x0001
#define XUSB_GAMEPAD_DPAD_DOWN      0x0002
#define XUSB_GAMEPAD_DPAD_LEFT      0x0004
#define XUSB_GAMEPAD_DPAD_RIGHT     0x0008
#endif

namespace windeck::mapper {

using namespace windeck::neptune;

void DpadMapper::apply_config(const config::DpadConfig& cfg) {
    mode_ = cfg.mode;
    switch (mode_) {
        case config::DpadMode::Wasd:   directional_.set_keys_wasd(); break;
        case config::DpadMode::Arrows: directional_.set_keys_arrows(); break;
        default: break;
    }
}

USHORT DpadMapper::map(uint32_t nb, windeck::output::SyntheticInput& synth) {
    bool up    = (nb & btn::kDpadUp) != 0;
    bool down  = (nb & btn::kDpadDown) != 0;
    bool left  = (nb & btn::kDpadLeft) != 0;
    bool right = (nb & btn::kDpadRight) != 0;

    switch (mode_) {
        case config::DpadMode::Dpad: {
            USHORT xb = 0;
            if (up)    xb |= XUSB_GAMEPAD_DPAD_UP;
            if (down)  xb |= XUSB_GAMEPAD_DPAD_DOWN;
            if (left)  xb |= XUSB_GAMEPAD_DPAD_LEFT;
            if (right) xb |= XUSB_GAMEPAD_DPAD_RIGHT;
            return xb;
        }
        case config::DpadMode::Wasd:
        case config::DpadMode::Arrows:
            directional_.update_discrete(up, down, left, right, synth);
            return 0;
    }
    return 0;
}

void DpadMapper::release_all(windeck::output::SyntheticInput& synth) {
    directional_.release_all(synth);
}

} // namespace windeck::mapper
