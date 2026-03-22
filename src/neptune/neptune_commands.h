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
constexpr uint8_t kReportIdSetRumble   = 0x8F;

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

struct ClearMappingsReport_v2 {
    uint8_t report_id = kReportIdClearMappings;  // 0x81 as report ID
    uint8_t padding[64] = {};
};

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

struct DisableLizardSettings_v2 {
    uint8_t report_id = kReportIdSetSettings;    // 0x87 as report ID
    uint8_t data_len = 15;
    uint8_t s1_id = kSettingSteamWatchdogEnable;
    uint8_t s1_val_lo = 0;
    uint8_t s1_val_hi = 0;
    uint8_t s2_id = kSettingLeftTrackpadMode;
    uint8_t s2_val_lo = kTrackpadNone;
    uint8_t s2_val_hi = 0;
    uint8_t s3_id = kSettingRightTrackpadMode;
    uint8_t s3_val_lo = kTrackpadNone;
    uint8_t s3_val_hi = 0;
    uint8_t s4_id = kSettingLeftTrackpadClickPressure;
    uint8_t s4_val_lo = 0xFF;
    uint8_t s4_val_hi = 0xFF;
    uint8_t s5_id = kSettingRightTrackpadClickPressure;
    uint8_t s5_val_lo = 0xFF;
    uint8_t s5_val_hi = 0xFF;
    uint8_t padding[48] = {};
};

// Haptic feedback command
struct HapticReport {
    uint8_t report_id = 0x00;
    uint8_t command = kReportIdSetRumble;  // 0x8F
    uint8_t data_len = 0x07;
    uint8_t side;       // 0 = right, 1 = left
    uint16_t amplitude;
    uint16_t period;
    uint16_t count;
    uint8_t padding[54] = {};
};

} // namespace windeck::neptune
