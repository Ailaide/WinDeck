#pragma once

#include "neptune_report.h"
#include <hidapi.h>
#include <memory>

namespace windeck::neptune {

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

    // Read the next input report (blocking with timeout).
    // Returns: 1 = new report read, 0 = timeout (no data), -1 = error
    int read(InputReport& report);

    // Send haptic feedback to the controller.
    // side: 0 = right, 1 = left
    void send_haptic(uint8_t side, uint16_t amplitude, uint16_t period, uint16_t count);

    // Returns true if the device is currently open.
    bool is_open() const { return device_ != nullptr; }

    // Send the heartbeat to keep lizard mode disabled.
    void send_heartbeat();

private:
    // Disable lizard mode (keyboard/mouse emulation) so we get raw gamepad input.
    void disable_lizard_mode();

    // Re-enable lizard mode (restore default keyboard/mouse emulation on exit).
    void enable_lizard_mode();

    // Send a feature report. Returns true on success.
    bool send_feature_report(const uint8_t* data, size_t len);
    // Send feature report and read back acknowledgment (required for SET_SETTINGS).
    bool send_feature_report_with_ack(const uint8_t* data, size_t len);

    hid_device* device_ = nullptr;
    uint64_t timeout_count_ = 0;
    uint64_t report_count_ = 0;
};

} // namespace windeck::neptune
