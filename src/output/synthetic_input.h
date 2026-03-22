#pragma once

#include <cstdint>

// Skeleton for the unified SendInput module.
// In the MVP this is a placeholder — keyboard/mouse output comes later.

namespace windeck::output {

class SyntheticInput {
public:
    // Aggregate all pending keyboard/mouse actions and send in one SendInput call.
    void flush();

    // Reset all pressed keys (called on profile switch or shutdown).
    void release_all();
};

} // namespace windeck::output
