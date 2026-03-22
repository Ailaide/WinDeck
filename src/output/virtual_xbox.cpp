#include "output/virtual_xbox.h"
#include <spdlog/spdlog.h>

namespace windeck::output {

VirtualXbox::~VirtualXbox() {
    disconnect();
}

bool VirtualXbox::connect() {
    if (client_) {
        spdlog::warn("ViGEm client already connected");
        return true;
    }

    client_ = vigem_alloc();
    if (!client_) {
        spdlog::error("Failed to allocate ViGEm client");
        return false;
    }

    VIGEM_ERROR err = vigem_connect(client_);
    if (!VIGEM_SUCCESS(err)) {
        spdlog::error("Failed to connect to ViGEmBus (error {:#x}). Is the driver installed?",
                       static_cast<unsigned>(err));
        vigem_free(client_);
        client_ = nullptr;
        return false;
    }

    pad_ = vigem_target_x360_alloc();
    if (!pad_) {
        spdlog::error("Failed to allocate Xbox 360 controller target");
        vigem_disconnect(client_);
        vigem_free(client_);
        client_ = nullptr;
        return false;
    }

    err = vigem_target_add(client_, pad_);
    if (!VIGEM_SUCCESS(err)) {
        spdlog::error("Failed to add virtual Xbox 360 controller (error {:#x})",
                       static_cast<unsigned>(err));
        vigem_target_free(pad_);
        pad_ = nullptr;
        vigem_disconnect(client_);
        vigem_free(client_);
        client_ = nullptr;
        return false;
    }

    // Register rumble callback
    err = vigem_target_x360_register_notification(client_, pad_, rumble_callback, this);
    if (!VIGEM_SUCCESS(err)) {
        spdlog::warn("Failed to register rumble callback (error {:#x})",
                       static_cast<unsigned>(err));
        // Non-fatal — rumble just won't work
    }

    spdlog::info("Virtual Xbox 360 controller connected");
    return true;
}

void VirtualXbox::disconnect() {
    if (pad_) {
        vigem_target_x360_unregister_notification(pad_);
        vigem_target_remove(client_, pad_);
        vigem_target_free(pad_);
        pad_ = nullptr;
    }
    if (client_) {
        vigem_disconnect(client_);
        vigem_free(client_);
        client_ = nullptr;
    }
}

void VirtualXbox::update(const XUSB_REPORT& report) {
    if (!client_ || !pad_) return;
    vigem_target_x360_update(client_, pad_, report);
}

RumbleState VirtualXbox::get_rumble() const {
    uint64_t packed = rumble_state_.load(std::memory_order_relaxed);
    return RumbleState{
        static_cast<uint8_t>((packed >> 8) & 0xFF),
        static_cast<uint8_t>(packed & 0xFF)
    };
}

void CALLBACK VirtualXbox::rumble_callback(
    PVIGEM_CLIENT /*client*/, PVIGEM_TARGET /*target*/,
    UCHAR large_motor, UCHAR small_motor, UCHAR /*led_number*/,
    LPVOID user_data)
{
    auto* self = static_cast<VirtualXbox*>(user_data);
    uint64_t packed = (static_cast<uint64_t>(large_motor) << 8) | small_motor;
    self->rumble_state_.store(packed, std::memory_order_relaxed);
}

} // namespace windeck::output
