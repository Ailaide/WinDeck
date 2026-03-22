#pragma once

#include <cstdint>

// Neptune controller HID input report structure for Steam Deck
// ALL button bits verified on real hardware (2026-03-21)
//
// The Neptune controller sends 64-byte input reports at ~250Hz (every 4ms).
// On Windows via hidapi, byte 0 is the report ID (0x01).
// Byte 2 = message type: 0x09 = Deck controller input.

namespace windeck::neptune {

// Button bitmask flags for buttons field (offset 8-11)
// VERIFIED on real Steam Deck hardware
namespace btn {
    constexpr uint32_t kRTrigFull   = 1 << 0;   // Right trigger full press (digital)
    constexpr uint32_t kLTrigFull   = 1 << 1;   // Left trigger full press (digital)
    constexpr uint32_t kRShoulder   = 1 << 2;   // RB
    constexpr uint32_t kLShoulder   = 1 << 3;   // LB
    constexpr uint32_t kY           = 1 << 4;   // Y (north)
    constexpr uint32_t kB           = 1 << 5;   // B (east)
    constexpr uint32_t kX           = 1 << 6;   // X (west)
    constexpr uint32_t kA           = 1 << 7;   // A (south)
    constexpr uint32_t kDpadUp      = 1 << 8;
    constexpr uint32_t kDpadRight   = 1 << 9;
    constexpr uint32_t kDpadLeft    = 1 << 10;
    constexpr uint32_t kDpadDown    = 1 << 11;
    constexpr uint32_t kMenu        = 1 << 12;  // Back / View (☰)
    constexpr uint32_t kSteam       = 1 << 13;  // Steam button (SYSTEM)
    constexpr uint32_t kOptions     = 1 << 14;  // Start / Menu (≡)
    constexpr uint32_t kL5          = 1 << 15;  // Back paddle left lower
    constexpr uint32_t kR5          = 1 << 16;  // Back paddle right lower
    constexpr uint32_t kLPadTouch   = 1 << 17;  // Left touchpad finger touch
    constexpr uint32_t kRPadTouch   = 1 << 18;  // Right touchpad finger touch
    constexpr uint32_t kLPadClick   = 1 << 19;  // Left touchpad press
    constexpr uint32_t kRPadClick   = 1 << 20;  // Right touchpad press
    // bits 21: unverified (possibly trigger touch)
    constexpr uint32_t kLStickClick = 1 << 22;  // Left stick press (LS)
    // bits 23-25: unverified (possibly stick/trigger touch)
    constexpr uint32_t kRStickClick = 1 << 26;  // Right stick press (RS)
}

// Button bitmask flags for buttons_ext field (offset 12-15)
// VERIFIED on real Steam Deck hardware
namespace btn_ext {
    constexpr uint32_t kL4          = 1 << 9;   // Back paddle left upper
    constexpr uint32_t kR4          = 1 << 10;  // Back paddle right upper
    constexpr uint32_t kQuickAccess = 1 << 18;  // "..." quick access button (SYSTEM)
}

// Raw 64-byte HID input report
// Layout verified against actual Steam Deck hex dumps (2026-03-21)
#pragma pack(push, 1)
struct InputReport {
    // Header (bytes 0-3)
    uint8_t  report_id;          // 0x01 (Windows HID report ID)
    uint8_t  unknown1;           // 0x00
    uint8_t  msg_type;           // 0x09 = Deck input
    uint8_t  msg_length;         // 0x40 = 64

    // Sequence (bytes 4-7)
    uint32_t seq_number;

    // Buttons (bytes 8-15) — Steam Deck has TWO 32-bit button fields
    uint32_t buttons;            // Primary buttons (see btn:: namespace)
    uint32_t buttons_ext;        // Extended buttons (see btn_ext:: namespace)

    // Touchpad coordinates (bytes 16-23) — (-32768 to 32767)
    int16_t  left_pad_x;
    int16_t  left_pad_y;
    int16_t  right_pad_x;
    int16_t  right_pad_y;

    // Accelerometer (bytes 24-29) — raw 16-bit, ±2g, 16384/g
    int16_t  accel_x;
    int16_t  accel_y;
    int16_t  accel_z;

    // Gyroscope (bytes 30-35) — raw 16-bit, ±2000 dps
    int16_t  gyro_pitch;
    int16_t  gyro_yaw;
    int16_t  gyro_roll;

    // Orientation quaternion (bytes 36-43)
    int16_t  orient_w;
    int16_t  orient_x;
    int16_t  orient_y;
    int16_t  orient_z;

    // Analog triggers (bytes 44-47) — 0 to 32767
    uint16_t left_trigger;
    uint16_t right_trigger;

    // Analog sticks (bytes 48-55) — -32768 to 32767
    int16_t  left_stick_x;
    int16_t  left_stick_y;
    int16_t  right_stick_x;
    int16_t  right_stick_y;

    // Touchpad pressure (bytes 56-59)
    uint16_t left_pad_pressure;
    uint16_t right_pad_pressure;

    // Remaining bytes (60-63)
    uint8_t  padding[4];
};
#pragma pack(pop)

static_assert(sizeof(InputReport) == 64, "InputReport must be exactly 64 bytes");

} // namespace windeck::neptune
