#include "driver/driver_manager.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <urlmon.h>
#include <shellapi.h>
#include <shlobj.h>
#include <ViGEm/Client.h>
#include <spdlog/spdlog.h>

#include <filesystem>

#pragma comment(lib, "urlmon.lib")

namespace windeck::driver {

namespace fs = std::filesystem;

// URL for the latest ViGEmBus release (LizardByte maintained fork)
static constexpr const wchar_t* kViGEmBusInstallerUrl =
    L"https://github.com/nefarius/ViGEmBus/releases/download/v1.22.0/ViGEmBus_1.22.0_x64_x86_arm64.exe";

static constexpr const wchar_t* kViGEmBusInstallerName =
    L"ViGEmBus_Setup.exe";

DriverStatus check_vigembus() {
    PVIGEM_CLIENT client = vigem_alloc();
    if (!client) {
        spdlog::debug("Failed to allocate ViGEm client for driver check");
        return DriverStatus::kCheckFailed;
    }

    VIGEM_ERROR err = vigem_connect(client);
    if (VIGEM_SUCCESS(err)) {
        vigem_disconnect(client);
        vigem_free(client);
        return DriverStatus::kInstalled;
    }

    vigem_free(client);
    spdlog::debug("ViGEmBus connection failed with error {:#x}", static_cast<unsigned>(err));
    return DriverStatus::kNotInstalled;
}

// Download a file from URL to a local path. Returns true on success.
static bool download_file(const wchar_t* url, const wchar_t* dest_path) {
    spdlog::info("Downloading ViGEmBus installer...");
    HRESULT hr = URLDownloadToFileW(nullptr, url, dest_path, 0, nullptr);
    if (FAILED(hr)) {
        spdlog::error("Download failed (HRESULT {:#x})", static_cast<unsigned>(hr));
        return false;
    }
    spdlog::info("Download complete");
    return true;
}

// Run an installer with UAC elevation and wait for it to finish.
static bool run_installer_elevated(const wchar_t* path) {
    spdlog::info("Launching ViGEmBus installer (UAC elevation required)...");

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = path;
    // ViGEmBus .exe installer uses no command-line args for standard install.
    // It's a self-extracting NSIS or similar installer — just run it.
    sei.lpParameters = nullptr;
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        DWORD err = GetLastError();
        if (err == ERROR_CANCELLED) {
            spdlog::warn("UAC elevation was cancelled by user");
        } else {
            spdlog::error("Failed to launch installer (error {})", err);
        }
        return false;
    }

    // Wait for the installer to finish (up to 60 seconds)
    if (sei.hProcess) {
        spdlog::info("Waiting for installation to complete...");
        DWORD wait_result = WaitForSingleObject(sei.hProcess, 300000);
        if (wait_result == WAIT_TIMEOUT) {
            spdlog::warn("Installer timed out after 5 minutes");
        }

        DWORD exit_code = 0;
        GetExitCodeProcess(sei.hProcess, &exit_code);
        CloseHandle(sei.hProcess);

        if (exit_code != 0) {
            spdlog::warn("Installer exited with code {}", exit_code);
            // Some installers return non-zero for "reboot required" etc.
            // We'll check the actual driver status after.
        }
    }

    return true;
}

bool ensure_vigembus_installed() {
    // First check if already installed
    DriverStatus status = check_vigembus();
    if (status == DriverStatus::kInstalled) {
        spdlog::info("ViGEmBus driver is already installed");
        return true;
    }

    spdlog::warn("ViGEmBus driver is not installed");

    // Determine download path: %TEMP%\ViGEmBus_Setup.exe
    wchar_t temp_path[MAX_PATH];
    GetTempPathW(MAX_PATH, temp_path);
    fs::path installer_path = fs::path(temp_path) / kViGEmBusInstallerName;

    // Download installer
    if (!download_file(kViGEmBusInstallerUrl, installer_path.c_str())) {
        spdlog::error("Failed to download ViGEmBus installer");
        spdlog::error("Please install manually from: https://github.com/nefarius/ViGEmBus/releases");
        return false;
    }

    // Run installer with elevation
    if (!run_installer_elevated(installer_path.c_str())) {
        spdlog::error("Failed to run ViGEmBus installer");
        return false;
    }

    // Clean up downloaded installer
    std::error_code ec;
    fs::remove(installer_path, ec);

    // Verify installation succeeded
    status = check_vigembus();
    if (status == DriverStatus::kInstalled) {
        spdlog::info("ViGEmBus driver installed successfully");
        return true;
    }

    spdlog::error("ViGEmBus driver installation may require a reboot");
    return false;
}

} // namespace windeck::driver
