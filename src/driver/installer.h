#pragma once

#include <cstdint>

namespace windeck::driver {

// A pinned third-party installer. The SHA-256 must match the file published at
// `url`; a mismatch aborts the installation before anything is executed.
struct InstallerSpec {
    const wchar_t* url;
    const char*    sha256;     // 64 hex chars, uppercase
    const wchar_t* file_name;  // file name used for the download
    const wchar_t* arguments;  // installer command line (nullptr = none)
};

enum class InstallerResult {
    kFinished,        // installer ran to completion (see exit_code)
    kDownloadFailed,
    kHashMismatch,
    kCancelled,       // UAC prompt declined
    kLaunchFailed,
    kTimedOut
};

// Download the installer into a new private directory under %TEMP%, verify its
// SHA-256 and run it elevated (UAC), waiting up to timeout_ms. The file stays
// open with write/delete sharing denied from verification until the installer
// exits, so the verified bytes are exactly what runs. Downloads are removed
// afterwards. exit_code is set when the result is kFinished.
InstallerResult download_verify_and_run(const InstallerSpec& spec, uint32_t timeout_ms,
                                        uint32_t& exit_code);

} // namespace windeck::driver
