#include "mapper/trigger_mapper.h"

namespace windeck::mapper {

uint8_t TriggerMapper::map(uint16_t raw, windeck::output::SyntheticInput& synth) {
    // Optional key or mouse button, held while the trigger is past its threshold.
    windeck::output::HoldSet holds;
    if (has_button_) {
        float normalized = static_cast<float>(raw) / 32767.0f;
        if (normalized >= button_threshold_) holds.add(button_action_);
    }
    synth.set_holds(source_, holds);

    return apply_trigger_deadzone(raw, threshold_);
}

void TriggerMapper::release_all(windeck::output::SyntheticInput& synth) {
    synth.clear_holds(source_);
}

} // namespace windeck::mapper
