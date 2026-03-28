#pragma once

#include <cstdint>

// Neptune controller HID feature report command IDs
// Reference: Linux kernel hid-steam.c, neptune-hidapi.net

namespace windeck::neptune {

constexpr uint16_t kVendorId  = 0x28DE;
constexpr uint16_t kProductId = 0x1205;

// Feature report IDs
constexpr uint8_t kReportIdInputData   = 0x09;
constexpr uint8_t kReportIdSetMappings = 0x80;
constexpr uint8_t kReportIdClearMappings = 0x81;
constexpr uint8_t kReportIdGetMappings = 0x82;
constexpr uint8_t kReportIdGetAttrib   = 0x83;
constexpr uint8_t kReportIdGetAttribLabel = 0x84;
constexpr uint8_t kReportIdSetSettings = 0x87;
constexpr uint8_t kReportIdClearSettings = 0x85;
constexpr uint8_t kReportIdGetSettings = 0x89;
constexpr uint8_t kReportIdResetDevice = 0x95;
constexpr uint8_t kReportIdHapticPulse = 0x8F;
constexpr uint8_t kReportIdHapticCmd   = 0xEA;
constexpr uint8_t kReportIdRumbleCmd   = 0xEB;

// Feature reports for Neptune controller.
// On Windows hidapi, byte 0 = HID report ID.
// Two report ID formats: 0x00 (command in payload) or command byte as report ID.

// Clear digital mappings — disables keyboard/mouse emulation.
struct ClearMappingsReport_v1 {
    uint8_t report_id = 0x00;
    uint8_t command = kReportIdClearMappings;  // 0x81
    uint8_t data_len = 0x00;
    uint8_t padding[62] = {};
};
static_assert(sizeof(ClearMappingsReport_v1) == 65, "Must be 65 bytes");


// Setting IDs from Linux hid-steam.c
constexpr uint8_t kSettingLeftTrackpadMode           = 7;
constexpr uint8_t kSettingRightTrackpadMode          = 8;
constexpr uint8_t kSettingLeftTrackpadClickPressure   = 52;
constexpr uint8_t kSettingRightTrackpadClickPressure  = 53;
constexpr uint8_t kSettingSteamWatchdogEnable         = 71;

constexpr uint8_t kTrackpadNone = 7;

// SET_SETTINGS — configure controller settings.
// Format: [report_id] [0x87] [length] [setting_id, val_lo, val_hi]...
// This is the proper way to disable lizard mode permanently (no heartbeat needed).
struct DisableLizardSettings_v1 {
    uint8_t report_id = 0x00;
    uint8_t command = kReportIdSetSettings;      // 0x87
    uint8_t data_len = 15;                       // 5 settings × 3 bytes = 15
    // Setting 1: disable watchdog
    uint8_t s1_id = kSettingSteamWatchdogEnable;
    uint8_t s1_val_lo = 0;
    uint8_t s1_val_hi = 0;
    // Setting 2: left trackpad mode = none
    uint8_t s2_id = kSettingLeftTrackpadMode;
    uint8_t s2_val_lo = kTrackpadNone;
    uint8_t s2_val_hi = 0;
    // Setting 3: right trackpad mode = none
    uint8_t s3_id = kSettingRightTrackpadMode;
    uint8_t s3_val_lo = kTrackpadNone;
    uint8_t s3_val_hi = 0;
    // Setting 4: left trackpad click pressure = max (disable click)
    uint8_t s4_id = kSettingLeftTrackpadClickPressure;
    uint8_t s4_val_lo = 0xFF;
    uint8_t s4_val_hi = 0xFF;
    // Setting 5: right trackpad click pressure = max (disable click)
    uint8_t s5_id = kSettingRightTrackpadClickPressure;
    uint8_t s5_val_lo = 0xFF;
    uint8_t s5_val_hi = 0xFF;
    uint8_t padding[47] = {};
};
static_assert(sizeof(DisableLizardSettings_v1) == 65, "Must be 65 bytes");


#pragma pack(push, 1)

// Haptic pulse command (single pulses, 0x8F)
struct HapticReport {
    uint8_t report_id = 0x00;
    uint8_t command = kReportIdHapticPulse;  // 0x8F
    uint8_t data_len = 0x07;
    uint8_t side;       // 0 = right, 1 = left
    uint16_t amplitude;
    uint16_t period;
    uint16_t count;
    uint8_t padding[55] = {};  // total = 10 + 55 = 65 bytes
};
static_assert(sizeof(HapticReport) == 65, "HapticReport must be 65 bytes");

// Trigger haptic command (0xEA) — advanced control with frequency and duration.
// Matches SDL's MsgTriggerHaptic struct.
struct TriggerHapticReport {
    uint8_t report_id = 0x00;
    uint8_t command = kReportIdHapticCmd;   // 0xEA
    uint8_t data_len = 0x11;               // 17 bytes payload
    uint8_t side;               // 0x00=L, 0x01=R, 0x03=Both (hardware-verified)
    uint8_t cmd;                // 0=off, 1=tick, 2=click, 3=tone, 4=rumble
    uint8_t ui_intensity = 0;   // 0-4, 0=default
    int8_t  db_gain;            // dB gain
    uint16_t freq;              // frequency in Hz
    int16_t dur_ms;             // duration in ms (-1 = infinite)
    uint16_t noise_intensity = 0;
    uint16_t lfo_freq = 0;
    uint8_t lfo_depth = 0;
    uint8_t rand_tone_gain = 0;
    uint8_t script_id = 0;
    uint16_t lss_start_freq = 0;
    uint16_t lss_end_freq = 0;
    uint8_t padding[43] = {};
};
static_assert(sizeof(TriggerHapticReport) == 65, "TriggerHapticReport must be 65 bytes");

// Simple rumble command (0xEB) — matches SDL's MsgSimpleRumbleCmd.
struct RumbleReport {
    uint8_t report_id = 0x00;
    uint8_t command = kReportIdRumbleCmd;  // 0xEB
    uint8_t data_len = 0x09;
    uint8_t rumble_type = 0;
    uint16_t intensity = 0;   // 0 = HAPTIC_INTENSITY_SYSTEM
    uint16_t left_speed;      // 0-65535
    uint16_t right_speed;     // 0-65535
    int8_t left_gain = 2;
    int8_t right_gain = 0;
    uint8_t padding[53] = {};
};
static_assert(sizeof(RumbleReport) == 65, "RumbleReport must be 65 bytes");

#pragma pack(pop)

} // namespace windeck::neptune
