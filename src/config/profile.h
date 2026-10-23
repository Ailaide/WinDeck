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
    MouseScroll,   // Emit one mouse wheel tick on press: code=1 up, code=0 down
    None           // No action
};

struct ButtonAction {
    ActionType type = ActionType::XboxButton;
    uint16_t   code = 0;  // XUSB_GAMEPAD_* or VK_* or mouse button index

    bool operator==(const ButtonAction&) const = default;
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
    bool          invert_x = false;
    bool          invert_y = false;
    float         overlap = 0.5f;  // for WASD/Arrows modes

    bool operator==(const StickConfig&) const = default;
};

// Trigger operating mode: the analog Xbox trigger, or bindings A, B and C.
enum class TriggerMode : uint8_t { Xbox, Buttons };

// Hold keeps the action pressed while the pull is past the depth; Immediate
// taps it once (kImmediateTapMs) when the depth is reached.
enum class BindingMode : uint8_t { Hold, Immediate };

// Haptic click on the trigger's side when a binding presses (and, in Hold
// mode, releases). Low is the touchpad edge cue's -4 dB, High is full.
enum class ClickStrength : uint8_t { None, Low, High };

// Limits shared by the profile loader, the mapper and the menu.
inline constexpr float kTriggerPullMin       = 0.10f;  // shallowest binding depth
inline constexpr float kTriggerPullMax       = 1.00f;  // needs the raw maximum 32767
inline constexpr float kTriggerDeadzoneMax   = 0.50f;
inline constexpr float kTriggerReleaseGap    = 0.05f;  // Hold releases this far below its depth
inline constexpr float kTriggerArmPull       = 0.05f;  // a disarmed trigger re-arms at or below this
inline constexpr int   kImmediateTapMs       = 50;     // how long an Immediate tap holds its action
inline constexpr size_t kTriggerBindings     = 3;      // A, B, C

struct TriggerBinding {
    ButtonAction  action = {ActionType::None, 0};
    float         pull   = 0.30f;   // depth 0..1 of the raw trigger value
    BindingMode   mode   = BindingMode::Hold;
    ClickStrength click  = ClickStrength::Low;

    bool operator==(const TriggerBinding&) const = default;
};

struct TriggerConfig {
    TriggerMode mode = TriggerMode::Xbox;
    float       deadzone = 0.02f;   // Xbox mode only
    // Kept in Xbox mode too, so switching modes never loses the bindings.
    std::array<TriggerBinding, kTriggerBindings> bindings = {{
        {{ActionType::None, 0}, 0.30f, BindingMode::Hold, ClickStrength::Low},
        {{ActionType::None, 0}, 0.60f, BindingMode::Hold, ClickStrength::Low},
        {{ActionType::None, 0}, 0.95f, BindingMode::Hold, ClickStrength::Low},
    }};

    bool operator==(const TriggerConfig&) const = default;
};

// D-Pad operating mode
enum class DpadMode : uint8_t { Dpad, Wasd, Arrows };

struct DpadConfig {
    DpadMode mode = DpadMode::Dpad;
    float    overlap = 0.5f;  // for WASD/Arrows modes

    bool operator==(const DpadConfig&) const = default;
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
    uint16_t     pressure_threshold = 6144; // 0-32767 (per hid-steam kernel driver)
    float        sensitivity = 1.0f;
    float        inertia = 0.95f;           // Trackball glide: 0.80=stops fast, 0.99=long glide
    TouchpadAccelerationPreset acceleration = TouchpadAccelerationPreset::Normal;
    ButtonAction click_action = {ActionType::MouseButton, 0}; // Default: LMB
    bool         invert_x = false;
    bool         invert_y = false;
    float        overlap = 0.5f;  // for WASD/Arrows/DpadButtons modes
    // Radial menu: 8 sectors, default keys 1-8 starting at 12 o'clock clockwise
    std::array<ButtonAction, 8> radial_keys = {{
        {ActionType::Keyboard, '1'}, {ActionType::Keyboard, '2'},
        {ActionType::Keyboard, '3'}, {ActionType::Keyboard, '4'},
        {ActionType::Keyboard, '5'}, {ActionType::Keyboard, '6'},
        {ActionType::Keyboard, '7'}, {ActionType::Keyboard, '8'},
    }};

    bool operator==(const TouchpadConfig&) const = default;
};

// Gyro operating mode
enum class GyroMode : uint8_t { Disabled, Mouse };

struct GyroConfig {
    GyroMode       mode = GyroMode::Disabled;
    float          sensitivity_x = 1.0f;
    float          sensitivity_y = 1.0f;
    bool           invert_x = false;
    bool           invert_y = false;

    bool operator==(const GyroConfig&) const = default;
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

        // Right touchpad: trackpad with inertia, LMB click at ~19% of max pressure (6144/32767)
        right_touchpad.mode = TouchpadMode::TrackpadInertia;
        right_touchpad.pressure_threshold = 6144;
        right_touchpad.click_action = {ActionType::MouseButton, 0};

        // Left touchpad: scroll
        left_touchpad.mode = TouchpadMode::Scroll;
    }

    bool operator==(const Profile&) const = default;
};

} // namespace windeck::config
