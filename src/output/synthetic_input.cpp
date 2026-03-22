#include "output/synthetic_input.h"

namespace windeck::output {

void SyntheticInput::key_press(uint16_t vk) {
    if (vk >= 256 || key_state_[vk]) return;
    key_state_[vk] = true;

    INPUT inp{};
    inp.type = INPUT_KEYBOARD;
    inp.ki.wVk = static_cast<WORD>(vk);
    inp.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    inp.ki.dwFlags = (vk >= 0x21 && vk <= 0x2E) ? KEYEVENTF_EXTENDEDKEY : 0;
    add_input(inp);
}

void SyntheticInput::key_release(uint16_t vk) {
    if (vk >= 256 || !key_state_[vk]) return;
    key_state_[vk] = false;

    INPUT inp{};
    inp.type = INPUT_KEYBOARD;
    inp.ki.wVk = static_cast<WORD>(vk);
    inp.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    inp.ki.dwFlags = KEYEVENTF_KEYUP | ((vk >= 0x21 && vk <= 0x2E) ? KEYEVENTF_EXTENDEDKEY : 0);
    add_input(inp);
}

void SyntheticInput::key_set(uint16_t vk, bool down) {
    if (down) key_press(vk); else key_release(vk);
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

void SyntheticInput::mouse_set(uint8_t index, bool down) {
    if (down) mouse_press(index); else mouse_release(index);
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
    if (pending_count_ > 0) {
        SendInput(static_cast<UINT>(pending_count_), pending_, sizeof(INPUT));
        pending_count_ = 0;
    }
}

void SyntheticInput::release_all() {
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
    if (pending_count_ >= kMaxInputs) {
        SendInput(static_cast<UINT>(pending_count_), pending_, sizeof(INPUT));
        pending_count_ = 0;
    }
}

} // namespace windeck::output
