#include "output/synthetic_input.h"

namespace windeck::output {

// Extended keys: navigation cluster, arrows, right-side modifiers, and the
// GUI/Application keys. These require KEYEVENTF_EXTENDEDKEY so the system sends
// the E0-prefixed scan code (and distinguishes them from numpad keys that share
// a scan code). The remap picker can bind Win/Menu, so they must be here too.
static bool is_extended_key(uint16_t vk) {
    switch (vk) {
        case VK_PRIOR: case VK_NEXT:
        case VK_END:   case VK_HOME:
        case VK_LEFT:  case VK_UP:  case VK_RIGHT: case VK_DOWN:
        case VK_INSERT: case VK_DELETE:
        case VK_RCONTROL: case VK_RMENU:
        case VK_LWIN: case VK_RWIN: case VK_APPS:
        case VK_DIVIDE: case VK_SNAPSHOT:
            return true;
        default:
            return false;
    }
}

SyntheticInput::SyntheticInput()
    : sender_([](const INPUT* inputs, UINT count) {
          SendInput(count, const_cast<INPUT*>(inputs), sizeof(INPUT));
      }) {}

SyntheticInput::SyntheticInput(Sender sender) : sender_(std::move(sender)) {}

void SyntheticInput::set_holds(HoldSource source, const HoldSet& holds) {
    sources_[static_cast<size_t>(source)] = holds;
}

// Shift, Ctrl, Alt and Win, generic and side-specific.
static bool is_modifier_key(uint16_t vk) {
    switch (vk) {
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        case VK_MENU: case VK_LMENU: case VK_RMENU:
        case VK_LWIN: case VK_RWIN:
            return true;
        default:
            return false;
    }
}

void SyntheticInput::reconcile_holds() {
    HoldSet wanted;
    for (const auto& s : sources_) {
        wanted.keys  |= s.keys;
        wanted.mouse |= s.mouse;
    }
    // Releases first, so nothing let go of applies to what is pressed next:
    // mouse buttons, then ordinary keys, then modifiers.
    for (uint8_t i = 0; i < 3; i++)
        if (!wanted.mouse[i]) mouse_release(i);
    for (uint16_t vk = 1; vk < 256; vk++)
        if (!wanted.keys[vk] && !is_modifier_key(vk)) key_release(vk);
    for (uint16_t vk = 1; vk < 256; vk++)
        if (!wanted.keys[vk] && is_modifier_key(vk)) key_release(vk);
    // Presses in the reverse order: modifiers are down before the key or mouse
    // button they modify, whichever sources hold them.
    for (uint16_t vk = 1; vk < 256; vk++)
        if (wanted.keys[vk] && is_modifier_key(vk)) key_press(vk);
    for (uint16_t vk = 1; vk < 256; vk++)
        if (wanted.keys[vk] && !is_modifier_key(vk)) key_press(vk);
    for (uint8_t i = 0; i < 3; i++)
        if (wanted.mouse[i]) mouse_press(i);
}

void SyntheticInput::key_press(uint16_t vk) {
    if (vk == 0 || vk >= 256 || key_state_[vk]) return;
    key_state_[vk] = true;

    WORD scan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    bool extended = is_extended_key(vk);

    INPUT inp{};
    inp.type       = INPUT_KEYBOARD;
    inp.ki.wScan   = scan;
    inp.ki.dwFlags = KEYEVENTF_SCANCODE | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
    // Fallback to VK if scan code is unavailable (e.g. some media keys)
    if (scan == 0) {
        inp.ki.wVk     = static_cast<WORD>(vk);
        inp.ki.dwFlags = extended ? KEYEVENTF_EXTENDEDKEY : 0;
    }
    add_input(inp);
}

void SyntheticInput::key_release(uint16_t vk) {
    if (vk >= 256 || !key_state_[vk]) return;
    key_state_[vk] = false;

    WORD scan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    bool extended = is_extended_key(vk);

    INPUT inp{};
    inp.type       = INPUT_KEYBOARD;
    inp.ki.wScan   = scan;
    inp.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
    if (scan == 0) {
        inp.ki.wVk     = static_cast<WORD>(vk);
        inp.ki.dwFlags = KEYEVENTF_KEYUP | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
    }
    add_input(inp);
}

void SyntheticInput::mouse_press(uint8_t index) {
    if (index >= 3 || mouse_state_[index]) return;
    mouse_state_[index] = true;

    INPUT inp{};
    inp.type = INPUT_MOUSE;
    switch (index) {
        case 0: inp.mi.dwFlags = MOUSEEVENTF_LEFTDOWN; break;
        case 1: inp.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN; break;
        case 2: inp.mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN; break;
    }
    add_input(inp);
}

void SyntheticInput::mouse_release(uint8_t index) {
    if (index >= 3 || !mouse_state_[index]) return;
    mouse_state_[index] = false;

    INPUT inp{};
    inp.type = INPUT_MOUSE;
    switch (index) {
        case 0: inp.mi.dwFlags = MOUSEEVENTF_LEFTUP; break;
        case 1: inp.mi.dwFlags = MOUSEEVENTF_RIGHTUP; break;
        case 2: inp.mi.dwFlags = MOUSEEVENTF_MIDDLEUP; break;
    }
    add_input(inp);
}

void SyntheticInput::mouse_move(int dx, int dy) {
    mouse_dx_ += dx;
    mouse_dy_ += dy;
}

void SyntheticInput::mouse_scroll(int delta) {
    mouse_scroll_ += delta;
}

void SyntheticInput::flush() {
    // Add accumulated mouse movement
    if (mouse_dx_ != 0 || mouse_dy_ != 0) {
        INPUT inp{};
        inp.type = INPUT_MOUSE;
        inp.mi.dx = mouse_dx_;
        inp.mi.dy = mouse_dy_;
        inp.mi.dwFlags = MOUSEEVENTF_MOVE;
        add_input(inp);
        mouse_dx_ = 0;
        mouse_dy_ = 0;
    }

    // Add accumulated scroll
    if (mouse_scroll_ != 0) {
        INPUT inp{};
        inp.type = INPUT_MOUSE;
        inp.mi.mouseData = static_cast<DWORD>(mouse_scroll_);
        inp.mi.dwFlags = MOUSEEVENTF_WHEEL;
        add_input(inp);
        mouse_scroll_ = 0;
    }

    // Send everything in one call
    send_pending();
}

void SyntheticInput::release_all() {
    for (auto& s : sources_) s = HoldSet{};
    for (uint16_t vk = 0; vk < 256; vk++) {
        if (key_state_[vk]) key_release(vk);
    }
    for (uint8_t i = 0; i < 3; i++) {
        if (mouse_state_[i]) mouse_release(i);
    }
    flush();
}

void SyntheticInput::add_input(const INPUT& inp) {
    if (pending_count_ < kMaxInputs) {
        pending_[pending_count_++] = inp;
    }
    // If buffer full, flush immediately
    if (pending_count_ >= kMaxInputs) send_pending();
}

void SyntheticInput::send_pending() {
    if (pending_count_ > 0) {
        sender_(pending_, static_cast<UINT>(pending_count_));
        pending_count_ = 0;
    }
}

} // namespace windeck::output
