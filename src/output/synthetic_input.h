#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <array>

namespace windeck::output {

// Unified keyboard/mouse output via SendInput.
// Tracks key state to avoid redundant presses and ensure clean release.
// All pending actions are batched into a single SendInput call per frame.
class SyntheticInput {
public:
    // Press a virtual key (VK code). Idempotent if already pressed.
    void key_press(uint16_t vk);

    // Release a virtual key. Idempotent if already released.
    void key_release(uint16_t vk);

    // Set key state: press if down=true, release if down=false.
    void key_set(uint16_t vk, bool down);

    // Press a mouse button. index: 0=left, 1=right, 2=middle.
    void mouse_press(uint8_t index);

    // Release a mouse button.
    void mouse_release(uint8_t index);

    // Set mouse button state.
    void mouse_set(uint8_t index, bool down);

    // Add relative mouse movement (accumulated per frame).
    void mouse_move(int dx, int dy);

    // Add mouse wheel scroll (accumulated per frame).
    void mouse_scroll(int delta);

    // Flush all pending actions in a single SendInput call.
    // Call once per frame after all mappers have run.
    void flush();

    // Release all pressed keys and mouse buttons (profile switch / shutdown).
    void release_all();

private:
    // Track pressed state for up to 256 VK codes
    std::array<bool, 256> key_state_{};

    // Mouse button state (left, right, middle)
    std::array<bool, 3> mouse_state_{};

    // Pending inputs for this frame
    static constexpr int kMaxInputs = 64;
    INPUT pending_[kMaxInputs]{};
    int pending_count_ = 0;

    // Accumulated mouse delta
    int mouse_dx_ = 0;
    int mouse_dy_ = 0;
    int mouse_scroll_ = 0;

    void add_input(const INPUT& inp);
};

} // namespace windeck::output
