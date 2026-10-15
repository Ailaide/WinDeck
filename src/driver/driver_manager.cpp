#include "driver/driver_manager.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ViGEm/Client.h>
#include <spdlog/spdlog.h>

#include "driver/installer.h"

namespace windeck::driver {

// ViGEmBus 1.22.0, the final release of nefarius/ViGEmBus (archived upstream).
// SHA-256 matches the winget manifest for ViGEm.ViGEmBus 1.22.0.
static constexpr InstallerSpec kViGEmBusInstaller = {
    L"https://github.com/nefarius/ViGEmBus/releases/download/v1.22.0/ViGEmBus_1.22.0_x64_x86_arm64.exe",
    "89220A7865076B342892F98865F3499FB7C4CFD673159E89D352C360FD014C6A",
    L"ViGEmBus_Setup.exe",
    nullptr,  // interactive install, no command-line arguments
};

static constexpr uint32_t kViGEmBusInstallTimeoutMs = 5 * 60 * 1000;

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

bool ensure_vigembus_installed() {
    // First check if already installed
    DriverStatus status = check_vigembus();
    if (status == DriverStatus::kInstalled) {
        spdlog::info("ViGEmBus driver is already installed");
        return true;
    }

    spdlog::warn("ViGEmBus driver is not installed");

    spdlog::info("Downloading ViGEmBus installer (UAC elevation will be requested)...");
    uint32_t exit_code = 0;
    switch (download_verify_and_run(kViGEmBusInstaller, kViGEmBusInstallTimeoutMs, exit_code)) {
        case InstallerResult::kFinished:
            // Some installers return non-zero for "reboot required" etc.;
            // the actual driver status is checked below.
            if (exit_code != 0) spdlog::warn("Installer exited with code {}", exit_code);
            break;
        case InstallerResult::kTimedOut:
            spdlog::warn("Installer timed out after 5 minutes");
            break;
        default:
            spdlog::error("Failed to install ViGEmBus driver");
            spdlog::error("Please install manually from: https://github.com/nefarius/ViGEmBus/releases");
            return false;
    }

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
