#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "config/profile.h"
#include <array>
#include <bitset>
#include <cstdint>
#include <functional>

namespace windeck::output {

// Every producer of held keys and mouse buttons. Each keeps its own set, so a
// source letting go of a key never lifts it while another source still holds it.
// Parts of one control that update on their own (a touchpad's directional keys,
// its click and its radial menu) are separate sources.
enum class HoldSource : uint8_t {
    Buttons,
    LeftStick,
    RightStick,
    Dpad,
    LeftPadDirectional,
    RightPadDirectional,
    LeftPadClick,
    RightPadClick,
    LeftPadRadial,
    RightPadRadial,
    LeftTrigger,
    RightTrigger,
    Count
};

// The keys and mouse buttons one source wants held.
struct HoldSet {
    std::bitset<256> keys;   // VK codes; 0 is never held
    std::bitset<3>   mouse;  // 0 = left, 1 = right, 2 = middle

    void add_key(uint16_t vk) { if (vk > 0 && vk < 256) keys.set(vk); }
    void add_mouse(uint8_t index) { if (index < 3) mouse.set(index); }

    // Keyboard and mouse-button actions only; other action types hold nothing.
    void add(const config::ButtonAction& action) {
        if (action.type == config::ActionType::Keyboard) add_key(action.code);
        else if (action.type == config::ActionType::MouseButton && action.code < 3)
            add_mouse(static_cast<uint8_t>(action.code));
    }

    bool empty() const { return keys.none() && mouse.none(); }
    bool operator==(const HoldSet&) const = default;
};

// Unified keyboard/mouse output via SendInput, and the single owner of every
// held key and mouse button. All pending input is batched into one SendInput
// call per flush().
class SyntheticInput {
public:
    using Sender = std::function<void(const INPUT* inputs, UINT count)>;

    // Sends through SendInput.
    SyntheticInput();
    // Sends through `sender` instead (unit tests capture the input).
    explicit SyntheticInput(Sender sender);

    // Replace the whole set of keys and mouse buttons `source` wants held. A
    // source that is not updated keeps its last set. Nothing changes on the
    // system until reconcile_holds().
    void set_holds(HoldSource source, const HoldSet& holds);
    void clear_holds(HoldSource source) { set_holds(source, HoldSet{}); }

    // Press every key and mouse button that some source wants and release the
    // rest: one press or release per output, however many sources share it.
    // Releases are queued before presses (mouse buttons, keys, then modifiers),
    // and presses run the other way (modifiers, keys, then mouse buttons), so a
    // modifier is always down before the key or click it belongs to.
    void reconcile_holds();

    // Add relative mouse movement (accumulated per frame).
    void mouse_move(int dx, int dy);

    // Add mouse wheel scroll (accumulated per frame; one-off, never held).
    void mouse_scroll(int delta);

    // Flush all pending actions in a single SendInput call.
    // Call once per frame after all mappers have run.
    void flush();

    // Release every pressed key and mouse button and forget every source's set
    // (menu open, profile switch, shutdown). Sends at once.
    void release_all();

    // Current output state, for tests and diagnostics.
    bool key_down(uint16_t vk) const { return vk < 256 && key_state_[vk]; }
    bool mouse_down(uint8_t index) const { return index < 3 && mouse_state_[index]; }

private:
    Sender sender_;

    std::array<HoldSet, static_cast<size_t>(HoldSource::Count)> sources_{};

    // What has been sent: pressed keys (VK codes) and mouse buttons.
    std::array<bool, 256> key_state_{};
    std::array<bool, 3>   mouse_state_{};

    // Pending inputs for this frame
    static constexpr int kMaxInputs = 64;
    INPUT pending_[kMaxInputs]{};
    int pending_count_ = 0;

    // Accumulated mouse delta
    int mouse_dx_ = 0;
    int mouse_dy_ = 0;
    int mouse_scroll_ = 0;

    // Idempotent: only a change of state queues input.
    void key_press(uint16_t vk);
    void key_release(uint16_t vk);
    void mouse_press(uint8_t index);
    void mouse_release(uint8_t index);

    void add_input(const INPUT& inp);
    void send_pending();
};

} // namespace windeck::output
