#pragma once

#include "neptune_report.h"
#include <hidapi.h>
#include <memory>

namespace windeck::neptune {

// Which grip motor a haptic click plays on.
enum class HapticSide : uint8_t { Left, Right };

class NeptuneDevice {
public:
    NeptuneDevice() = default;
    ~NeptuneDevice();

    NeptuneDevice(const NeptuneDevice&) = delete;
    NeptuneDevice& operator=(const NeptuneDevice&) = delete;

    // Open the Neptune controller HID device.
    // Returns true on success.
    bool open();

    // Close the device and release resources.
    void close();

    // Read the next input report, waiting at most `timeout_ms` (0 = do not wait).
    // Returns: 1 = new report read, 0 = timeout (no data), -1 = error
    int read(InputReport& report, int timeout_ms = 4);

    // Send single haptic pulse (report 0x8F). side: 0 = right, 1 = left
    void send_haptic(uint8_t side, uint16_t amplitude, uint16_t period, uint16_t count);

    // Send continuous rumble (matching SDL's ID_TRIGGER_RUMBLE_CMD).
    void send_rumble(uint16_t left_speed, uint16_t right_speed);

    // Send advanced haptic command (report 0xEA).
    // side: 0 = left, 1 = right (hardware-verified), 3 = both.
    // cmd: 0=off, 1=tick, 2=click, 3=tone, 4=rumble.
    void send_haptic_cmd(uint8_t side, uint8_t cmd, int8_t gain_db);

    // A short click on one grip motor (report 0xEA, cmd 2). Returns false, and
    // logs at debug level, when the write fails.
    bool click_haptic(HapticSide side, int8_t gain_db);

    // Stop all haptics.
    void stop_haptics();

    // Returns true if the device is currently open.
    bool is_open() const { return device_ != nullptr; }

    // Re-enable lizard mode (restore default keyboard/mouse emulation on exit).
    // Public because emergency restore needs to call it from input thread.
    void enable_lizard_mode();

private:
    // Disable lizard mode (keyboard/mouse emulation) so we get raw gamepad input.
    void disable_lizard_mode();

    // Send a feature report. Returns true on success.
    bool send_feature_report(const uint8_t* data, size_t len);
    // Send feature report and read back acknowledgment (required for SET_SETTINGS).
    bool send_feature_report_with_ack(const uint8_t* data, size_t len);

    hid_device* device_ = nullptr;
    uint64_t timeout_count_ = 0;
    uint64_t report_count_ = 0;
};

} // namespace windeck::neptune
