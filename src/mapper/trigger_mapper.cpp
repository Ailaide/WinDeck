#include "mapper/trigger_mapper.h"

namespace windeck::mapper {

uint8_t TriggerMapper::map(uint16_t raw, windeck::output::SyntheticInput& synth) {
    // Trigger button action (optional)
    if (has_button_) {
        float normalized = static_cast<float>(raw) / 32767.0f;
        bool should_press = normalized >= button_threshold_;
        if (should_press && !button_pressed_) {
            switch (button_action_.type) {
                case config::ActionType::Keyboard:
                    synth.key_set(button_action_.code, true);
                    break;
                case config::ActionType::MouseButton:
                    synth.mouse_set(static_cast<uint8_t>(button_action_.code), true);
                    break;
                default: break;
            }
        } else if (!should_press && button_pressed_) {
            switch (button_action_.type) {
                case config::ActionType::Keyboard:
                    synth.key_set(button_action_.code, false);
                    break;
                case config::ActionType::MouseButton:
                    synth.mouse_set(static_cast<uint8_t>(button_action_.code), false);
                    break;
                default: break;
            }
        }
        button_pressed_ = should_press;
    }

    return apply_trigger_deadzone(raw, threshold_);
}

} // namespace windeck::mapper
