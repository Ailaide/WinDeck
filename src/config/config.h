#pragma once

#include "config/profile.h"
#include <string>
#include <shared_mutex>
#include <filesystem>

namespace windeck::config {

// Manages profiles: load/save JSON, hot reload, thread-safe access.
class Config {
public:
    Config();

    // Load a profile by name. Returns true on success.
    bool load_profile(const std::string& name);

    // Save current profile to disk.
    bool save_profile() const;

    // Get a read-only copy of the current profile (thread-safe).
    Profile get_profile() const;

    // Replace the current profile (thread-safe, write lock).
    void set_profile(const Profile& profile);

    // Get the config directory path (%APPDATA%/WinDeck/).
    const std::filesystem::path& config_dir() const { return config_dir_; }

    // List available profile names.
    std::vector<std::string> list_profiles() const;

private:
    void ensure_dirs() const;
    std::filesystem::path profile_path(const std::string& name) const;

    std::filesystem::path config_dir_;
    mutable std::shared_mutex mutex_;
    Profile profile_;
};

// Serialize profile to/from JSON string.
std::string profile_to_json(const Profile& profile);
Profile profile_from_json(const std::string& json_str);

} // namespace windeck::config
