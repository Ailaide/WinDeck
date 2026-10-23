#include "neptune/neptune_device.h"
#include "neptune/neptune_commands.h"
#include <spdlog/spdlog.h>
#include <cstring>

namespace windeck::neptune {

NeptuneDevice::~NeptuneDevice() {
    close();
}

bool NeptuneDevice::open() {
    if (device_) {
        spdlog::warn("Neptune device already open");
        return true;
    }

    if (hid_init() != 0) {
        spdlog::error("Failed to initialize hidapi");
        return false;
    }

    // Enumerate to log available devices
    struct hid_device_info* devs = hid_enumerate(kVendorId, kProductId);
    if (devs) {
        for (auto* cur = devs; cur; cur = cur->next) {
            spdlog::info("Found HID device: VID={:#06x} PID={:#06x} interface={} usage_page={:#06x} usage={:#06x} path={}",
                cur->vendor_id, cur->product_id, cur->interface_number,
                cur->usage_page, cur->usage, cur->path ? cur->path : "null");
        }
        hid_free_enumeration(devs);
    } else {
        spdlog::error("No Neptune HID devices found (VID={:#06x}, PID={:#06x})", kVendorId, kProductId);
        hid_exit();
        return false;
    }

    // Open the vendor-specific interface (usage_page=0xFFFF) which carries raw gamepad data.
    // hid_open() picks the first interface (mouse), which is wrong.
    std::string target_path;
    devs = hid_enumerate(kVendorId, kProductId);
    for (auto* cur = devs; cur; cur = cur->next) {
        if (cur->usage_page == 0xFFFF && cur->usage == 0x0001) {
            target_path = cur->path;
            spdlog::info("Selected vendor-specific interface: {}", target_path);
            break;
        }
    }
    hid_free_enumeration(devs);

    if (target_path.empty()) {
        spdlog::error("No vendor-specific HID interface found for Neptune controller");
        hid_exit();
        return false;
    }

    device_ = hid_open_path(target_path.c_str());
    if (!device_) {
        spdlog::error("Failed to open Neptune controller at path: {}", target_path);
        hid_exit();
        return false;
    }

    // Set non-blocking mode off — we use hid_read_timeout instead
    hid_set_nonblocking(device_, 0);

    spdlog::info("Neptune controller opened successfully");

    disable_lizard_mode();
    return true;
}

void NeptuneDevice::close() {
    if (device_) {
        enable_lizard_mode();
        hid_close(device_);
        device_ = nullptr;
        hid_exit();
        spdlog::info("Neptune controller closed");
    }
}

int NeptuneDevice::read(InputReport& report, int timeout_ms) {
    if (!device_) return -1;

    uint8_t buf[64];
    // The caller passes the controller's 4 ms report interval, or less when a
    // trigger tap ends sooner.
    int bytes = hid_read_timeout(device_, buf, sizeof(buf), timeout_ms < 0 ? 0 : timeout_ms);

    if (bytes < 0) {
        spdlog::error("HID read error");
        return -1;
    }

    if (bytes == 0) {
        // Timeout — no new report available
        timeout_count_++;
        if (timeout_count_ == 250) { // ~1 second of timeouts
            spdlog::warn("No HID data received for ~1 second ({} timeouts)", timeout_count_);
        }
        return 0;
    }

    timeout_count_ = 0;

    report_count_++;

    // Try to find the right report — accept any report with enough data
    if (bytes < 4) {
        spdlog::debug("Too short HID report: {} bytes", bytes);
        return 0;
    }

    // Validate report: expect report_id=0x01, msg_type=0x09 (Deck input)
    if (buf[0] != 0x01 || (bytes > 2 && buf[2] != 0x09)) {
        return 0; // Not a Deck input report, skip silently
    }

    if (bytes >= static_cast<int>(sizeof(InputReport))) {
        std::memcpy(&report, buf, sizeof(InputReport));
        return 1;
    }

    if (bytes >= 20) {
        std::memset(&report, 0, sizeof(InputReport));
        std::memcpy(&report, buf, bytes);
        return 1;
    }

    return 0;
}

bool NeptuneDevice::send_feature_report(const uint8_t* data, size_t len) {
    if (!device_) return false;
    int res = hid_send_feature_report(device_, data, len);
    if (res < 0) {
        spdlog::trace("Feature report send failed");
        return false;
    }
    return true;
}

bool NeptuneDevice::send_feature_report_with_ack(const uint8_t* data, size_t len) {
    if (!send_feature_report(data, len)) return false;

    // Read back the response — Linux hid-steam.c does steam_recv_report()
    // after steam_write_settings() to ensure the controller acknowledges.
    uint8_t buf[65] = {};
    buf[0] = data[0]; // Same report ID
    int res = hid_get_feature_report(device_, buf, sizeof(buf));
    if (res < 0) {
        spdlog::trace("Feature report ack read failed (non-critical)");
    }
    return true;
}

void NeptuneDevice::disable_lizard_mode() {
    if (!device_) return;

    // Implementation follows Linux hid-steam.c steam_set_lizard_mode(false).
    // Report ID = 0x00 confirmed as the only correct format.

    // Step 1: Clear digital mappings (disables keyboard/mouse emulation)
    ClearMappingsReport_v1 clear_cmd;
    if (send_feature_report(
            reinterpret_cast<const uint8_t*>(&clear_cmd), sizeof(clear_cmd))) {
        spdlog::info("Clear digital mappings sent");
    } else {
        spdlog::warn("Failed to send clear digital mappings");
    }

    // Step 2: Disable watchdog + configure trackpads via SET_SETTINGS (0x87).
    // Must read back response (ack) — Linux driver does steam_recv_report()
    // after steam_write_settings() to ensure controller processes the command.
    DisableLizardSettings_v1 settings_cmd;
    if (send_feature_report_with_ack(
            reinterpret_cast<const uint8_t*>(&settings_cmd), sizeof(settings_cmd))) {
        spdlog::info("Watchdog disabled via SET_SETTINGS (with ack) — no heartbeat needed");
    } else {
        spdlog::warn("Failed to disable watchdog via SET_SETTINGS");
    }
}

void NeptuneDevice::enable_lizard_mode() {
    if (!device_) return;

    // Step 1: Restore default digital mappings
    uint8_t cmd1[65] = {};
    cmd1[0] = 0x00;
    cmd1[1] = 0x85;  // ID_SET_DEFAULT_DIGITAL_MAPPINGS
    bool ok1 = send_feature_report(cmd1, sizeof(cmd1));

    // Step 2: Load default settings (resets trackpad modes, etc.)
    uint8_t cmd2[65] = {};
    cmd2[0] = 0x00;
    cmd2[1] = 0x8E;  // ID_LOAD_DEFAULT_SETTINGS
    bool ok2 = send_feature_report_with_ack(cmd2, sizeof(cmd2));

    // Step 3: Explicitly re-enable watchdog (LOAD_DEFAULT_SETTINGS may not reset it)
    uint8_t cmd3[65] = {};
    cmd3[0] = 0x00;
    cmd3[1] = kReportIdSetSettings;  // 0x87
    cmd3[2] = 3;                     // 1 setting × 3 bytes
    cmd3[3] = kSettingSteamWatchdogEnable;
    cmd3[4] = 1;                     // enable
    cmd3[5] = 0;
    bool ok3 = send_feature_report_with_ack(cmd3, sizeof(cmd3));

    spdlog::info("Lizard mode restore: mappings={} defaults={} watchdog={}", ok1, ok2, ok3);
    spdlog::default_logger()->flush();
}

void NeptuneDevice::send_haptic(uint8_t side, uint16_t amplitude,
                                 uint16_t period, uint16_t count) {
    if (!device_) return;

    HapticReport cmd;
    cmd.side = side;
    cmd.amplitude = amplitude;
    cmd.period = period;
    cmd.count = count;

    hid_send_feature_report(device_,
        reinterpret_cast<const uint8_t*>(&cmd), sizeof(cmd));
}

void NeptuneDevice::send_rumble(uint16_t left_speed, uint16_t right_speed) {
    if (!device_) return;

    RumbleReport cmd;
    cmd.left_speed = left_speed;
    cmd.right_speed = right_speed;

    hid_send_feature_report(device_,
        reinterpret_cast<const uint8_t*>(&cmd), sizeof(cmd));
}

void NeptuneDevice::send_haptic_cmd(uint8_t side, uint8_t haptic_cmd, int8_t gain_db) {
    if (!device_) return;

    TriggerHapticReport report;
    report.side = side;
    report.cmd = haptic_cmd;
    report.db_gain = gain_db;

    hid_send_feature_report(device_,
        reinterpret_cast<const uint8_t*>(&report), sizeof(report));
}

bool NeptuneDevice::click_haptic(HapticSide side, int8_t gain_db) {
    if (!device_) return false;

    // {}: every field without a default (frequency, duration) is zero, so each
    // click sends the same report.
    TriggerHapticReport report{};
    report.side = (side == HapticSide::Left) ? 0 : 1;   // 0xEA numbering, hardware-verified
    report.cmd = 2;                                      // click
    report.db_gain = gain_db;

    int written = hid_send_feature_report(device_,
        reinterpret_cast<const uint8_t*>(&report), sizeof(report));
    if (written < 0) {
        spdlog::debug("Haptic click write failed ({} side)",
                      side == HapticSide::Left ? "left" : "right");
        return false;
    }
    return true;
}

void NeptuneDevice::stop_haptics() {
    if (!device_) return;

    TriggerHapticReport cmd;
    cmd.side = 3;      // both
    cmd.cmd = 0;       // off
    cmd.db_gain = 0;
    cmd.freq = 0;
    cmd.dur_ms = 0;

    hid_send_feature_report(device_,
        reinterpret_cast<const uint8_t*>(&cmd), sizeof(cmd));
}

} // namespace windeck::neptune
