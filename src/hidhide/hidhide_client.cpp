#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <devguid.h>
#include <cfgmgr32.h>

#include "hidhide/hidhide_client.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <urlmon.h>
#include <shellapi.h>
#include <shlobj.h>
#pragma comment(lib, "urlmon.lib")

// HidHide IOCTL codes
// Device type 32769 (0x8001), METHOD_BUFFERED, FILE_READ_DATA
#define HIDHIDE_DEVICE_TYPE 32769
#define IOCTL_GET_WHITELIST CTL_CODE(HIDHIDE_DEVICE_TYPE, 2048, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_SET_WHITELIST CTL_CODE(HIDHIDE_DEVICE_TYPE, 2049, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_GET_BLACKLIST CTL_CODE(HIDHIDE_DEVICE_TYPE, 2050, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_SET_BLACKLIST CTL_CODE(HIDHIDE_DEVICE_TYPE, 2051, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_GET_ACTIVE    CTL_CODE(HIDHIDE_DEVICE_TYPE, 2052, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_SET_ACTIVE    CTL_CODE(HIDHIDE_DEVICE_TYPE, 2053, METHOD_BUFFERED, FILE_READ_DATA)

static const wchar_t* kHidHideDevicePath = L"\\\\.\\HidHide";

namespace windeck::hidhide {

HidHideClient::~HidHideClient() {
    if (neptune_hidden_) {
        unhide_neptune();
    }
}

void* HidHideClient::open_driver() {
    HANDLE h = CreateFileW(kHidHideDevicePath, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    return (h == INVALID_HANDLE_VALUE) ? nullptr : h;
}

bool HidHideClient::is_available() {
    HANDLE h = static_cast<HANDLE>(open_driver());
    if (!h) return false;
    CloseHandle(h);
    return true;
}

// Parse double-null-terminated wide string into vector
static std::vector<std::wstring> parse_multi_string(const wchar_t* buf, size_t chars) {
    std::vector<std::wstring> result;
    const wchar_t* p = buf;
    const wchar_t* end = buf + chars;
    while (p < end && *p != L'\0') {
        std::wstring s(p);
        result.push_back(s);
        p += s.size() + 1;
    }
    return result;
}

// Build double-null-terminated wide string from vector
static std::vector<wchar_t> build_multi_string(const std::vector<std::wstring>& list) {
    std::vector<wchar_t> buf;
    for (auto& s : list) {
        buf.insert(buf.end(), s.begin(), s.end());
        buf.push_back(L'\0');
    }
    buf.push_back(L'\0'); // Double null terminator
    return buf;
}

bool HidHideClient::get_string_list(unsigned long ioctl, std::vector<std::wstring>& list) {
    HANDLE h = static_cast<HANDLE>(open_driver());
    if (!h) return false;

    // First call: get required buffer size
    DWORD needed = 0;
    DeviceIoControl(h, ioctl, nullptr, 0, nullptr, 0, &needed, nullptr);

    if (needed == 0) {
        CloseHandle(h);
        list.clear();
        return true;
    }

    std::vector<wchar_t> buf(needed / sizeof(wchar_t) + 1, L'\0');
    DWORD returned = 0;
    BOOL ok = DeviceIoControl(h, ioctl, nullptr, 0,
        buf.data(), static_cast<DWORD>(buf.size() * sizeof(wchar_t)),
        &returned, nullptr);
    CloseHandle(h);

    if (!ok) return false;

    list = parse_multi_string(buf.data(), returned / sizeof(wchar_t));
    return true;
}

bool HidHideClient::set_string_list(unsigned long ioctl, const std::vector<std::wstring>& list) {
    HANDLE h = static_cast<HANDLE>(open_driver());
    if (!h) return false;

    auto buf = build_multi_string(list);
    DWORD returned = 0;
    BOOL ok = DeviceIoControl(h, ioctl,
        buf.data(), static_cast<DWORD>(buf.size() * sizeof(wchar_t)),
        nullptr, 0, &returned, nullptr);
    CloseHandle(h);
    return ok != 0;
}

bool HidHideClient::hide_device(const std::wstring& device_instance_path) {
    std::vector<std::wstring> blacklist;
    get_string_list(IOCTL_GET_BLACKLIST, blacklist);

    // Check if already in list
    for (auto& s : blacklist) {
        if (_wcsicmp(s.c_str(), device_instance_path.c_str()) == 0) return true;
    }

    blacklist.push_back(device_instance_path);
    return set_string_list(IOCTL_SET_BLACKLIST, blacklist);
}

bool HidHideClient::unhide_device(const std::wstring& device_instance_path) {
    std::vector<std::wstring> blacklist;
    get_string_list(IOCTL_GET_BLACKLIST, blacklist);

    blacklist.erase(
        std::remove_if(blacklist.begin(), blacklist.end(),
            [&](const std::wstring& s) {
                return _wcsicmp(s.c_str(), device_instance_path.c_str()) == 0;
            }),
        blacklist.end());

    return set_string_list(IOCTL_SET_BLACKLIST, blacklist);
}

bool HidHideClient::whitelist_app(const std::wstring& app_full_path) {
    std::vector<std::wstring> whitelist;
    get_string_list(IOCTL_GET_WHITELIST, whitelist);

    for (auto& s : whitelist) {
        if (_wcsicmp(s.c_str(), app_full_path.c_str()) == 0) return true;
    }

    whitelist.push_back(app_full_path);
    return set_string_list(IOCTL_SET_WHITELIST, whitelist);
}

bool HidHideClient::unwhitelist_app(const std::wstring& app_full_path) {
    std::vector<std::wstring> whitelist;
    get_string_list(IOCTL_GET_WHITELIST, whitelist);

    whitelist.erase(
        std::remove_if(whitelist.begin(), whitelist.end(),
            [&](const std::wstring& s) {
                return _wcsicmp(s.c_str(), app_full_path.c_str()) == 0;
            }),
        whitelist.end());

    return set_string_list(IOCTL_SET_WHITELIST, whitelist);
}

bool HidHideClient::set_active(bool active) {
    HANDLE h = static_cast<HANDLE>(open_driver());
    if (!h) return false;

    BOOLEAN val = active ? TRUE : FALSE;
    DWORD returned = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_SET_ACTIVE,
        &val, sizeof(val), nullptr, 0, &returned, nullptr);
    CloseHandle(h);
    return ok != 0;
}

bool HidHideClient::is_active() {
    HANDLE h = static_cast<HANDLE>(open_driver());
    if (!h) return false;

    BOOLEAN val = FALSE;
    DWORD returned = 0;
    DeviceIoControl(h, IOCTL_GET_ACTIVE,
        nullptr, 0, &val, sizeof(val), &returned, nullptr);
    CloseHandle(h);
    return val != FALSE;
}

static bool download_and_install_hidhide() {
    spdlog::info("Downloading HidHide installer...");

    wchar_t temp_path[MAX_PATH];
    GetTempPathW(MAX_PATH, temp_path);
    std::wstring installer_path = std::wstring(temp_path) + L"HidHide_Installer.exe";

    // Download latest release from nefarius
    const wchar_t* url = L"https://github.com/nefarius/HidHide/releases/download/v1.5.230.0/HidHide_1.5.230_x64.exe";
    HRESULT hr = URLDownloadToFileW(nullptr, url, installer_path.c_str(), 0, nullptr);
    if (FAILED(hr)) {
        spdlog::error("Failed to download HidHide installer (HRESULT {:#x})", static_cast<unsigned>(hr));
        return false;
    }

    spdlog::info("Download complete, launching installer (UAC elevation required)...");

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = installer_path.c_str();
    sei.lpParameters = L"/quiet /norestart";
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        spdlog::error("Failed to launch HidHide installer");
        return false;
    }

    spdlog::info("Waiting for installation to complete...");
    WaitForSingleObject(sei.hProcess, 60000); // 60s timeout

    DWORD exit_code = 0;
    GetExitCodeProcess(sei.hProcess, &exit_code);
    CloseHandle(sei.hProcess);

    DeleteFileW(installer_path.c_str());

    if (exit_code == 0) {
        spdlog::info("HidHide installed successfully");
        return true;
    }

    // Some installers return 3010 for "reboot required"
    if (exit_code == 3010) {
        spdlog::warn("HidHide installed but requires a reboot to take effect");
        return true;
    }

    spdlog::warn("HidHide installer exited with code {}", exit_code);
    return false;
}

bool HidHideClient::hide_neptune() {
    if (!is_available()) {
        spdlog::info("HidHide driver not installed, attempting auto-install...");
        if (!download_and_install_hidhide() || !is_available()) {
            spdlog::warn("HidHide not available — Neptune will be visible to other apps");
            return false;
        }
    }

    // Find ALL Neptune device instance paths via SetupAPI (mouse, keyboard, vendor interfaces)
    HDEVINFO dev_info = SetupDiGetClassDevsW(nullptr, L"HID", nullptr,
        DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (dev_info == INVALID_HANDLE_VALUE) return false;

    SP_DEVINFO_DATA dev_data{};
    dev_data.cbSize = sizeof(dev_data);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(dev_info, i, &dev_data); i++) {
        wchar_t hw_id[512] = {};
        if (!SetupDiGetDeviceRegistryPropertyW(dev_info, &dev_data,
                SPDRP_HARDWAREID, nullptr,
                reinterpret_cast<PBYTE>(hw_id), sizeof(hw_id), nullptr)) {
            continue;
        }

        std::wstring hw(hw_id);
        if (hw.find(L"VID_28DE") != std::wstring::npos &&
            hw.find(L"PID_1205") != std::wstring::npos) {

            wchar_t instance_id[256] = {};
            if (SetupDiGetDeviceInstanceIdW(dev_info, &dev_data,
                    instance_id, sizeof(instance_id) / sizeof(wchar_t), nullptr)) {
                neptune_instance_paths_.push_back(instance_id);
            }
        }
    }
    SetupDiDestroyDeviceInfoList(dev_info);

    if (neptune_instance_paths_.empty()) {
        spdlog::warn("Could not find Neptune device instances for HidHide");
        return false;
    }

    // Whitelist our own exe
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    if (!whitelist_app(exe_path)) {
        spdlog::warn("Failed to whitelist WinDeck in HidHide");
    }

    // Add all Neptune interfaces to blacklist
    for (auto& path : neptune_instance_paths_) {
        if (!hide_device(path)) {
            spdlog::warn("Failed to hide Neptune interface: {}",
                std::string(path.begin(), path.end()));
        }
    }

    set_active(true);
    neptune_hidden_ = true;

    spdlog::info("Neptune controller hidden via HidHide ({} interfaces)", neptune_instance_paths_.size());
    return true;
}

bool HidHideClient::unhide_neptune() {
    if (!neptune_hidden_ || neptune_instance_paths_.empty()) return false;

    for (auto& path : neptune_instance_paths_) {
        unhide_device(path);
    }
    neptune_hidden_ = false;

    spdlog::info("Neptune controller unhidden via HidHide");
    return true;
}

} // namespace windeck::hidhide
