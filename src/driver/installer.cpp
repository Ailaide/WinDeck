#include "driver/installer.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <urlmon.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <string>
#include <vector>

namespace windeck::driver {

namespace fs = std::filesystem;

namespace {

// Create a new, uniquely named directory under %TEMP%. CreateDirectoryW fails
// on an existing path, so the directory is guaranteed to be ours.
bool create_download_dir(fs::path& dir) {
    wchar_t temp_path[MAX_PATH];
    DWORD len = GetTempPathW(MAX_PATH, temp_path);
    if (len == 0 || len > MAX_PATH) return false;

    for (int attempt = 0; attempt < 16; ++attempt) {
        fs::path candidate = fs::path(temp_path) /
            (L"WinDeck-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
             std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt));
        if (CreateDirectoryW(candidate.c_str(), nullptr)) {
            dir = candidate;
            return true;
        }
        if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
    }
    return false;
}

// SHA-256 of the whole file behind `file` as uppercase hex; empty on failure.
std::string sha256_hex(HANDLE file) {
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(file, zero, nullptr, FILE_BEGIN)) return {};

    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        return {};
    }

    std::string result;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0))) {
        std::vector<UCHAR> buf(64 * 1024);
        bool ok = true;
        for (;;) {
            DWORD read = 0;
            if (!ReadFile(file, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr)) {
                ok = false;
                break;
            }
            if (read == 0) break;
            if (!BCRYPT_SUCCESS(BCryptHashData(hash, buf.data(), read, 0))) {
                ok = false;
                break;
            }
        }

        UCHAR digest[32];
        if (ok && BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0))) {
            static constexpr char kHex[] = "0123456789ABCDEF";
            for (UCHAR b : digest) {
                result += kHex[b >> 4];
                result += kHex[b & 0xF];
            }
        }
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return result;
}

// Run an installer with UAC elevation and wait for it to finish.
InstallerResult run_elevated(const fs::path& path, const wchar_t* arguments,
                             uint32_t timeout_ms, uint32_t& exit_code) {
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = path.c_str();
    sei.lpParameters = arguments;
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        DWORD err = GetLastError();
        if (err == ERROR_CANCELLED) {
            spdlog::warn("UAC elevation was cancelled by user");
            return InstallerResult::kCancelled;
        }
        spdlog::error("Failed to launch installer (error {})", err);
        return InstallerResult::kLaunchFailed;
    }
    if (!sei.hProcess) {
        spdlog::error("Installer started without a process handle");
        return InstallerResult::kLaunchFailed;
    }

    spdlog::info("Waiting for installation to complete...");
    if (WaitForSingleObject(sei.hProcess, timeout_ms) == WAIT_TIMEOUT) {
        CloseHandle(sei.hProcess);
        return InstallerResult::kTimedOut;
    }

    DWORD code = 0;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    exit_code = code;
    return InstallerResult::kFinished;
}

} // namespace

InstallerResult download_verify_and_run(const InstallerSpec& spec, uint32_t timeout_ms,
                                        uint32_t& exit_code) {
    fs::path dir;
    if (!create_download_dir(dir)) {
        spdlog::error("Failed to create a download directory in %TEMP%");
        return InstallerResult::kDownloadFailed;
    }
    const fs::path path = dir / spec.file_name;

    InstallerResult result = InstallerResult::kDownloadFailed;
    HRESULT hr = URLDownloadToFileW(nullptr, spec.url, path.c_str(), 0, nullptr);
    if (FAILED(hr)) {
        spdlog::error("Download failed (HRESULT {:#x})", static_cast<unsigned>(hr));
    } else {
        // Keep the file open with write/delete sharing denied until the installer
        // exits, so nothing can replace it between verification and execution.
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            spdlog::error("Failed to open downloaded installer (error {})", GetLastError());
        } else {
            const std::string actual = sha256_hex(file);
            if (actual != spec.sha256) {
                spdlog::error("Installer SHA-256 mismatch: expected {}, got {}",
                    spec.sha256, actual.empty() ? "<unavailable>" : actual);
                result = InstallerResult::kHashMismatch;
            } else {
                spdlog::info("Installer SHA-256 verified");
                result = run_elevated(path, spec.arguments, timeout_ms, exit_code);
            }
            CloseHandle(file);
        }
    }

    DeleteFileW(path.c_str());
    RemoveDirectoryW(dir.c_str());
    return result;
}

} // namespace windeck::driver
