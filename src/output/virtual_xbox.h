#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ViGEm/Client.h>
#include <functional>
#include <atomic>

namespace windeck::output {

// Rumble state received from games via the virtual Xbox controller.
struct RumbleState {
    uint8_t left_motor  = 0;
    uint8_t right_motor = 0;
};

class VirtualXbox {
public:
    VirtualXbox() = default;
    ~VirtualXbox();

    VirtualXbox(const VirtualXbox&) = delete;
    VirtualXbox& operator=(const VirtualXbox&) = delete;

    // Connect to ViGEmBus and create a virtual Xbox 360 controller.
    bool connect();

    // Disconnect and release the virtual controller.
    void disconnect();

    // Send the current input state to the virtual controller.
    void update(const XUSB_REPORT& report);

    // Get the latest rumble state (lock-free, called from input thread).
    RumbleState get_rumble() const;

    bool is_connected() const { return client_ != nullptr && pad_ != nullptr; }

private:
    static void CALLBACK rumble_callback(
        PVIGEM_CLIENT client, PVIGEM_TARGET target,
        UCHAR large_motor, UCHAR small_motor, UCHAR led_number,
        LPVOID user_data);

    PVIGEM_CLIENT client_ = nullptr;
    PVIGEM_TARGET pad_    = nullptr;
    std::atomic<uint64_t> rumble_state_{0}; // Packed: [left_motor << 8 | right_motor]
};

} // namespace windeck::output
