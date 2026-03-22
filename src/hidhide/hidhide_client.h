#pragma once

#include <string>
#include <vector>

namespace windeck::hidhide {

// Client for HidHide driver — hides HID devices from other applications.
// Requires HidHide driver to be installed.
class HidHideClient {
public:
    HidHideClient() = default;
    ~HidHideClient();

    HidHideClient(const HidHideClient&) = delete;
    HidHideClient& operator=(const HidHideClient&) = delete;

    // Check if HidHide driver is installed and accessible.
    bool is_available();

    // Hide a device by its instance path. Adds to blacklist.
    bool hide_device(const std::wstring& device_instance_path);

    // Unhide a device. Removes from blacklist.
    bool unhide_device(const std::wstring& device_instance_path);

    // Add an application to the whitelist (can see hidden devices).
    bool whitelist_app(const std::wstring& app_full_path);

    // Remove an application from the whitelist.
    bool unwhitelist_app(const std::wstring& app_full_path);

    // Enable/disable hiding globally.
    bool set_active(bool active);

    // Check if hiding is active.
    bool is_active();

    // Convenience: hide Neptune controller and whitelist our exe.
    bool hide_neptune();

    // Convenience: unhide Neptune (call on exit).
    bool unhide_neptune();

private:
    void* open_driver();
    bool get_string_list(unsigned long ioctl, std::vector<std::wstring>& list);
    bool set_string_list(unsigned long ioctl, const std::vector<std::wstring>& list);

    std::vector<std::wstring> neptune_instance_paths_;
    bool neptune_hidden_ = false;
};

} // namespace windeck::hidhide
