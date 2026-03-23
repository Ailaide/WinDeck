#pragma once

#include <cstdint>
#include <string>
#include <array>

namespace windeck::config {

// What a button press produces
enum class ActionType : uint8_t {
    XboxButton,    // Emit an Xbox 360 button
    Keyboard,      // Emit a keyboard key (VK code)
    MouseButton,   // Emit a mouse button (left/right/middle)
    None           // No action
};

struct ButtonAction {
    ActionType type = ActionType::XboxButton;
    uint16_t   code = 0;  // XUSB_GAMEPAD_* or VK_* or mouse button index
};

// Stick operating mode
enum class StickMode : uint8_t { Stick, Wasd, Arrows, Mouse };

// Response curve preset
enum class ResponseCurve : uint8_t { Linear, Quadratic, InverseQuadratic };

struct StickConfig {
    StickMode     mode  = StickMode::Stick;
    float         inner_deadzone = 0.05f;
    float         outer_deadzone = 0.95f;
    ResponseCurve curve = ResponseCurve::Linear;
    float         mouse_sensitivity = 1.0f;
    float         overlap = 0.0f;  // 0-1, diagonal overlap for wasd/arrows
};

struct TriggerConfig {
    float threshold = 0.02f;
    // Optional button action when trigger exceeds a digital threshold
    bool           has_button = false;
    float          button_threshold = 0.8f;
    ButtonAction   button_action;
};

// D-Pad operating mode
enum class DpadMode : uint8_t { Dpad, Wasd, Arrows };

struct DpadConfig {
    DpadMode mode = DpadMode::Dpad;
};

// Touchpad operating mode
enum class TouchpadMode : uint8_t {
    TrackpadInertia, Mouse, Wasd, Arrows,
    RadialMenu, DpadButtons, Scroll, Disabled
};

enum class TouchpadAccelerationPreset : uint8_t {
    Off,
    Normal,
    Strong
};

struct TouchpadConfig {
    TouchpadMode mode = TouchpadMode::TrackpadInertia;
    uint16_t     pressure_threshold = 50;   // 0-255
    float        sensitivity = 1.0f;
    float        friction = 0.95f;          // For inertia mode
    TouchpadAccelerationPreset acceleration = TouchpadAccelerationPreset::Normal;
    ButtonAction click_action = {ActionType::MouseButton, 0}; // Default: LMB
    // Radial menu: 8 sectors, default keys 1-8 starting at 12 o'clock clockwise
    std::array<ButtonAction, 8> radial_keys = {{
        {ActionType::Keyboard, '1'}, {ActionType::Keyboard, '2'},
        {ActionType::Keyboard, '3'}, {ActionType::Keyboard, '4'},
        {ActionType::Keyboard, '5'}, {ActionType::Keyboard, '6'},
        {ActionType::Keyboard, '7'}, {ActionType::Keyboard, '8'},
    }};
};

// Gyro operating mode
enum class GyroMode : uint8_t { Off, RightStick, Mouse };
enum class GyroActivation : uint8_t { Always, TouchRightPad, TouchLeftPad };

struct GyroConfig {
    GyroMode       mode = GyroMode::Off;
    float          sensitivity_x = 1.0f;
    float          sensitivity_y = 1.0f;
    GyroActivation activation = GyroActivation::Always;
};

// All remappable buttons on the controller (excluding system buttons)
enum class ButtonId : uint8_t {
    A, B, X, Y,
    LB, RB,
    Start, Back,
    LSClick, RSClick,
    L4, L5, R4, R5,
    LPadClick, RPadClick,
    Count  // sentinel
};

// Default Xbox mapping for each ButtonId
inline uint16_t default_xbox_button(ButtonId id) {
    switch (id) {
        case ButtonId::A:         return 0x1000; // XUSB_GAMEPAD_A
        case ButtonId::B:         return 0x2000; // XUSB_GAMEPAD_B
        case ButtonId::X:         return 0x4000; // XUSB_GAMEPAD_X
        case ButtonId::Y:         return 0x8000; // XUSB_GAMEPAD_Y
        case ButtonId::LB:        return 0x0100; // XUSB_GAMEPAD_LEFT_SHOULDER
        case ButtonId::RB:        return 0x0200; // XUSB_GAMEPAD_RIGHT_SHOULDER
        case ButtonId::Start:     return 0x0010; // XUSB_GAMEPAD_START
        case ButtonId::Back:      return 0x0020; // XUSB_GAMEPAD_BACK
        case ButtonId::LSClick:   return 0x0040; // XUSB_GAMEPAD_LEFT_THUMB
        case ButtonId::RSClick:   return 0x0080; // XUSB_GAMEPAD_RIGHT_THUMB
        case ButtonId::L4:        return 0;      // No default Xbox mapping
        case ButtonId::L5:        return 0;
        case ButtonId::R4:        return 0;
        case ButtonId::R5:        return 0;
        case ButtonId::LPadClick: return 0;
        case ButtonId::RPadClick: return 0;
        default:                  return 0;
    }
}

// Complete profile
struct Profile {
    std::string name = "Default";

    // Button remapping (indexed by ButtonId)
    std::array<ButtonAction, static_cast<size_t>(ButtonId::Count)> buttons;

    // Sticks
    StickConfig   left_stick;
    StickConfig   right_stick;

    // Triggers
    TriggerConfig left_trigger;
    TriggerConfig right_trigger;

    // D-Pad
    DpadConfig    dpad;

    // Touchpads
    TouchpadConfig left_touchpad;
    TouchpadConfig right_touchpad;

    // Gyro
    GyroConfig    gyro;

    // Initialize with defaults
    Profile() {
        for (size_t i = 0; i < buttons.size(); ++i) {
            auto id = static_cast<ButtonId>(i);
            uint16_t xbox = default_xbox_button(id);
            if (xbox != 0) {
                buttons[i] = {ActionType::XboxButton, xbox};
            } else {
                buttons[i] = {ActionType::None, 0};
            }
        }

        // Right touchpad: trackpad with inertia, LMB click at ~half max pressure (~1000)
        right_touchpad.mode = TouchpadMode::TrackpadInertia;
        right_touchpad.pressure_threshold = 6144;
        right_touchpad.click_action = {ActionType::MouseButton, 0};

        // Left touchpad: scroll
        left_touchpad.mode = TouchpadMode::Scroll;
    }
};

} // namespace windeck::config
