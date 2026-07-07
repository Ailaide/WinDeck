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

    // --- Profile management (Quick Access Menu) ---
    static constexpr const char* kDefaultProfileName = "Default";
    static bool is_default_name(const std::string& name) { return name == kDefaultProfileName; }

    // Name of the currently loaded (active) profile.
    std::string active_name() const;

    // Create a new profile from the standard Default mapping under the given
    // name and make it active. Refuses an empty name or "Default". Returns true
    // on success.
    bool create_from_default(const std::string& name);

    // Delete a profile file. Refuses "Default". If it was the active profile,
    // falls back to Default. Returns true on success.
    bool delete_profile(const std::string& name);

    // Last-active profile name persisted across runs (falls back to Default).
    std::string persisted_active() const;

    // Resolve a unique, non-reserved profile name. Empty base -> "Custom N";
    // a taken base gets " 2", " 3", ...; "Default" is always treated as taken.
    std::string unique_name(const std::string& base) const;

private:
    void ensure_dirs() const;
    std::filesystem::path profiles_dir() const { return config_dir_ / "profiles"; }
    std::filesystem::path profile_path(const std::string& name) const;
    bool write_profile(const std::filesystem::path& path, const Profile& p) const;
    std::filesystem::path active_marker_path() const;
    void persist_active(const std::string& name) const;

    std::filesystem::path config_dir_;
    mutable std::shared_mutex mutex_;
    Profile profile_;
};

// Serialize profile to/from JSON string.
std::string profile_to_json(const Profile& profile);
Profile profile_from_json(const std::string& json_str);

} // namespace windeck::config
