#pragma once

#include <string>

namespace windeck::driver {

enum class DriverStatus {
    kInstalled,
    kNotInstalled,
    kCheckFailed
};

// Check if ViGEmBus driver is installed by attempting a connection.
DriverStatus check_vigembus();

// Download and install ViGEmBus driver.
// Returns true if installation succeeded (or driver was already present).
// Shows progress in console and requests UAC elevation for the installer.
bool ensure_vigembus_installed();

} // namespace windeck::driver
