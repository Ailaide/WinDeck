#include "config/config.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <windows.h>

using json = nlohmann::json;

namespace windeck::config {

// --- JSON helpers ---

static const char* action_type_str(ActionType t) {
    switch (t) {
        case ActionType::XboxButton:  return "xbox";
        case ActionType::Keyboard:    return "keyboard";
        case ActionType::MouseButton: return "mouse";
        case ActionType::MouseScroll: return "scroll";
        default:                      return "none";
    }
}

static ActionType parse_action_type(const std::string& s) {
    if (s == "xbox")     return ActionType::XboxButton;
    if (s == "keyboard") return ActionType::Keyboard;
    if (s == "mouse")    return ActionType::MouseButton;
    if (s == "scroll")   return ActionType::MouseScroll;
    return ActionType::None;
}

static json action_to_json(const ButtonAction& a) {
    return {{"type", action_type_str(a.type)}, {"code", a.code}};
}

// --- Strict readers: a value of the wrong JSON type rejects the whole profile ---

[[noreturn]] static void reject(const std::string& what) {
    throw std::invalid_argument("profile: " + what);
}

static const json& require_object(const json& j, const char* what) {
    if (!j.is_object()) reject(std::string(what) + " must be an object");
    return j;
}

static float number_or(const json& j, const char* key, float def) {
    if (!j.contains(key)) return def;
    const json& v = j[key];
    if (!v.is_number()) reject(std::string(key) + " must be a number");
    return v.get<float>();
}

static std::string string_or(const json& j, const char* key, const char* def) {
    if (!j.contains(key)) return def;
    const json& v = j[key];
    if (!v.is_string()) reject(std::string(key) + " must be a string");
    return v.get<std::string>();
}

// Every Xbox 360 button bit (D-pad, Start, Back, thumbs, shoulders, Guide, A/B/X/Y);
// 0x0800 is reserved.
static constexpr uint32_t kXboxButtonMask = 0xF7FF;

// The action a JSON object describes, or None when it cannot produce output.
// The code is checked as the JSON number it is, before it is narrowed to 16 bits,
// so an out-of-range value can never wrap round to a different key.
static ButtonAction action_from_json(const json& j) {
    require_object(j, "action");
    const ActionType type = parse_action_type(string_or(j, "type", "none"));

    int64_t code = 0;
    if (j.contains("code")) {
        const json& v = j["code"];
        if (!v.is_number()) reject("code must be a number");
        if (v.is_number_float()) {
            double d = v.get<double>();
            if (d != std::floor(d) || d < 0.0 || d > 65535.0) return {ActionType::None, 0};
            code = static_cast<int64_t>(d);
        } else if (v.is_number_unsigned()) {
            uint64_t u = v.get<uint64_t>();
            if (u > 65535) return {ActionType::None, 0};
            code = static_cast<int64_t>(u);
        } else {
            code = v.get<int64_t>();
            if (code < 0 || code > 65535) return {ActionType::None, 0};
        }
    }

    bool valid = false;
    switch (type) {
        case ActionType::Keyboard:    valid = code >= 1 && code <= 255; break;
        case ActionType::MouseButton: valid = code <= 2; break;
        case ActionType::MouseScroll: valid = code <= 1; break;
        case ActionType::XboxButton:  valid = code != 0 && (code & ~int64_t{kXboxButtonMask}) == 0; break;
        case ActionType::None:        valid = false; break;
    }
    if (!valid) return {ActionType::None, 0};
    return {type, static_cast<uint16_t>(code)};
}

static const char* stick_mode_str(StickMode m) {
    switch (m) {
        case StickMode::Wasd:   return "wasd";
        case StickMode::Arrows: return "arrows";
        case StickMode::Mouse:  return "mouse";
        default:                return "stick";
    }
}

static StickMode parse_stick_mode(const std::string& s) {
    if (s == "wasd")   return StickMode::Wasd;
    if (s == "arrows") return StickMode::Arrows;
    if (s == "mouse")  return StickMode::Mouse;
    return StickMode::Stick;
}

static const char* curve_str(ResponseCurve c) {
    switch (c) {
        case ResponseCurve::Quadratic:        return "quadratic";
        case ResponseCurve::InverseQuadratic: return "inverse_quadratic";
        default:                              return "linear";
    }
}

static ResponseCurve parse_curve(const std::string& s) {
    if (s == "quadratic")         return ResponseCurve::Quadratic;
    if (s == "inverse_quadratic") return ResponseCurve::InverseQuadratic;
    return ResponseCurve::Linear;
}

static const char* dpad_mode_str(DpadMode m) {
    switch (m) {
        case DpadMode::Wasd:   return "wasd";
        case DpadMode::Arrows: return "arrows";
        default:               return "dpad";
    }
}

static DpadMode parse_dpad_mode(const std::string& s) {
    if (s == "wasd")   return DpadMode::Wasd;
    if (s == "arrows") return DpadMode::Arrows;
    return DpadMode::Dpad;
}

static const char* touchpad_mode_str(TouchpadMode m) {
    switch (m) {
        case TouchpadMode::Mouse:           return "mouse";
        case TouchpadMode::Wasd:            return "wasd";
        case TouchpadMode::Arrows:          return "arrows";
        case TouchpadMode::RadialMenu:      return "radial_menu";
        case TouchpadMode::DpadButtons:     return "dpad_buttons";
        case TouchpadMode::Scroll:          return "scroll";
        case TouchpadMode::Disabled:        return "disabled";
        default:                            return "trackpad_inertia";
    }
}

static TouchpadMode parse_touchpad_mode(const std::string& s) {
    if (s == "mouse")           return TouchpadMode::Mouse;
    if (s == "wasd")            return TouchpadMode::Wasd;
    if (s == "arrows")          return TouchpadMode::Arrows;
    if (s == "radial_menu")     return TouchpadMode::RadialMenu;
    if (s == "dpad_buttons")    return TouchpadMode::DpadButtons;
    if (s == "scroll")          return TouchpadMode::Scroll;
    if (s == "disabled")        return TouchpadMode::Disabled;
    return TouchpadMode::TrackpadInertia;
}

static const char* touchpad_accel_str(TouchpadAccelerationPreset p) {
    switch (p) {
        case TouchpadAccelerationPreset::Off:    return "off";
        case TouchpadAccelerationPreset::Strong: return "strong";
        default:                                 return "normal";
    }
}

static TouchpadAccelerationPreset parse_touchpad_accel(const std::string& s) {
    if (s == "off")    return TouchpadAccelerationPreset::Off;
    if (s == "strong") return TouchpadAccelerationPreset::Strong;
    return TouchpadAccelerationPreset::Normal;
}

static const char* trigger_mode_str(TriggerMode m) {
    return m == TriggerMode::Buttons ? "buttons" : "xbox";
}

static TriggerMode parse_trigger_mode(const std::string& s) {
    if (s == "buttons") return TriggerMode::Buttons;
    return TriggerMode::Xbox;
}

static const char* binding_mode_str(BindingMode m) {
    return m == BindingMode::Immediate ? "immediate" : "hold";
}

static BindingMode parse_binding_mode(const std::string& s) {
    if (s == "immediate") return BindingMode::Immediate;
    return BindingMode::Hold;
}

static const char* click_str(ClickStrength c) {
    switch (c) {
        case ClickStrength::None: return "none";
        case ClickStrength::High: return "high";
        default:                  return "low";
    }
}

static ClickStrength parse_click(const std::string& s) {
    if (s == "none") return ClickStrength::None;
    if (s == "high") return ClickStrength::High;
    return ClickStrength::Low;
}

// JSON keys of the three trigger bindings.
static constexpr const char* kBindingKeys[kTriggerBindings] = {"a", "b", "c"};

static const char* gyro_mode_str(GyroMode m) {
    switch (m) {
        case GyroMode::Mouse: return "mouse";
        default:              return "disabled";
    }
}

static GyroMode parse_gyro_mode(const std::string& s) {
    if (s == "mouse") return GyroMode::Mouse;
    return GyroMode::Disabled;
}

static const char* button_id_str(size_t i) {
    static const char* names[] = {
        "A", "B", "X", "Y", "LB", "RB", "Start", "Back",
        "LS", "RS", "L4", "L5", "R4", "R5", "LPadClick", "RPadClick"
    };
    return (i < std::size(names)) ? names[i] : "unknown";
}

static int button_id_from_str(const std::string& s) {
    static const char* names[] = {
        "A", "B", "X", "Y", "LB", "RB", "Start", "Back",
        "LS", "RS", "L4", "L5", "R4", "R5", "LPadClick", "RPadClick"
    };
    for (size_t i = 0; i < std::size(names); i++) {
        if (s == names[i]) return static_cast<int>(i);
    }
    return -1;
}

// --- Serialize ---

std::string profile_to_json(const Profile& p) {
    json j;
    j["name"] = p.name;

    // Buttons
    json btns = json::object();
    for (size_t i = 0; i < p.buttons.size(); i++) {
        btns[button_id_str(i)] = action_to_json(p.buttons[i]);
    }
    j["buttons"] = btns;

    // Sticks
    auto stick_json = [](const StickConfig& s) -> json {
        return {
            {"mode", stick_mode_str(s.mode)},
            {"inner_deadzone", s.inner_deadzone},
            {"outer_deadzone", s.outer_deadzone},
            {"curve", curve_str(s.curve)},
            {"mouse_sensitivity", s.mouse_sensitivity},
            {"invert_x", s.invert_x},
            {"invert_y", s.invert_y},
            {"overlap", s.overlap}
        };
    };
    j["left_stick"] = stick_json(p.left_stick);
    j["right_stick"] = stick_json(p.right_stick);

    // Triggers: mode, dead zone and all three bindings, whatever the mode.
    auto trig_json = [](const TriggerConfig& t) -> json {
        json tj = {
            {"mode", trigger_mode_str(t.mode)},
            {"deadzone", t.deadzone}
        };
        for (size_t i = 0; i < kTriggerBindings; i++) {
            const auto& b = t.bindings[i];
            tj[kBindingKeys[i]] = {
                {"pull", b.pull},
                {"action", action_to_json(b.action)},
                {"mode", binding_mode_str(b.mode)},
                {"click", click_str(b.click)}
            };
        }
        return tj;
    };
    j["left_trigger"] = trig_json(p.left_trigger);
    j["right_trigger"] = trig_json(p.right_trigger);

    // D-Pad
    j["dpad"] = {
        {"mode", dpad_mode_str(p.dpad.mode)},
        {"overlap", p.dpad.overlap}
    };

    // Touchpads
    auto pad_json = [](const TouchpadConfig& t) -> json {
        json pj = {
            {"mode", touchpad_mode_str(t.mode)},
            {"pressure_threshold", t.pressure_threshold},
            {"sensitivity", t.sensitivity},
            {"inertia", t.inertia},
            {"acceleration", touchpad_accel_str(t.acceleration)},
            {"click_action", action_to_json(t.click_action)},
            {"invert_x", t.invert_x},
            {"invert_y", t.invert_y},
            {"overlap", t.overlap}
        };
        if (t.mode == TouchpadMode::RadialMenu) {
            json rk = json::array();
            for (auto& k : t.radial_keys) rk.push_back(action_to_json(k));
            pj["radial_keys"] = rk;
        }
        return pj;
    };
    j["left_touchpad"] = pad_json(p.left_touchpad);
    j["right_touchpad"] = pad_json(p.right_touchpad);

    // Gyro
    j["gyro"] = {
        {"mode", gyro_mode_str(p.gyro.mode)},
        {"sensitivity_x", p.gyro.sensitivity_x},
        {"sensitivity_y", p.gyro.sensitivity_y},
        {"invert_x", p.gyro.invert_x},
        {"invert_y", p.gyro.invert_y}
    };

    return j.dump(2);
}

Profile profile_from_json(const std::string& json_str) {
    Profile p;
    json j = json::parse(json_str);

    p.name = j.value("name", "Default");

    // Buttons
    if (j.contains("buttons")) {
        auto& btns = j["buttons"];
        for (auto& [key, val] : btns.items()) {
            int idx = button_id_from_str(key);
            if (idx >= 0 && idx < static_cast<int>(p.buttons.size())) {
                p.buttons[idx] = action_from_json(val);
            }
        }
    }

    // Sticks
    auto parse_stick = [](const json& sj, StickConfig& s) {
        s.mode = parse_stick_mode(sj.value("mode", "stick"));
        s.inner_deadzone = std::clamp(sj.value("inner_deadzone", 0.05f), 0.0f, 1.0f);
        s.outer_deadzone = std::clamp(sj.value("outer_deadzone", 0.95f), 0.0f, 1.0f);
        s.curve = parse_curve(sj.value("curve", "linear"));
        s.mouse_sensitivity = std::clamp(sj.value("mouse_sensitivity", 1.0f), 0.1f, 20.0f);
        s.invert_x = sj.value("invert_x", false);
        s.invert_y = sj.value("invert_y", false);
        s.overlap = std::clamp(sj.value("overlap", 0.5f), 0.0f, 1.0f);
    };
    if (j.contains("left_stick"))  parse_stick(j["left_stick"], p.left_stick);
    if (j.contains("right_stick")) parse_stick(j["right_stick"], p.right_stick);

    // Triggers. Missing values keep the defaults; the old "threshold" and
    // "button" keys are ignored (no migration before 1.0).
    auto parse_trig = [](const json& tj, TriggerConfig& t) {
        require_object(tj, "trigger");
        const TriggerConfig defaults;
        t.mode = parse_trigger_mode(string_or(tj, "mode", "xbox"));
        t.deadzone = std::clamp(number_or(tj, "deadzone", defaults.deadzone),
                                0.0f, kTriggerDeadzoneMax);
        for (size_t i = 0; i < kTriggerBindings; i++) {
            auto& b = t.bindings[i];
            b = defaults.bindings[i];
            if (!tj.contains(kBindingKeys[i])) continue;
            const json& bj = require_object(tj[kBindingKeys[i]], "trigger binding");
            b.pull = std::clamp(number_or(bj, "pull", b.pull), kTriggerPullMin, kTriggerPullMax);
            if (bj.contains("action")) b.action = action_from_json(bj["action"]);
            b.mode = parse_binding_mode(string_or(bj, "mode", "hold"));
            b.click = parse_click(string_or(bj, "click", "low"));
        }
    };
    if (j.contains("left_trigger"))  parse_trig(j["left_trigger"], p.left_trigger);
    if (j.contains("right_trigger")) parse_trig(j["right_trigger"], p.right_trigger);

    // D-Pad
    if (j.contains("dpad")) {
        p.dpad.mode = parse_dpad_mode(j["dpad"].value("mode", "dpad"));
        p.dpad.overlap = std::clamp(j["dpad"].value("overlap", 0.5f), 0.0f, 1.0f);
    }

    // Touchpads
    auto parse_pad = [](const json& pj, TouchpadConfig& t) {
        t.mode = parse_touchpad_mode(pj.value("mode", "trackpad_inertia"));
        t.pressure_threshold = pj.value("pressure_threshold", (uint16_t)6144);
        t.sensitivity = std::clamp(pj.value("sensitivity", 1.0f), 0.1f, 20.0f);
        t.inertia = std::clamp(pj.value("inertia", 0.95f), 0.5f, 0.999f);
        t.acceleration = parse_touchpad_accel(pj.value("acceleration", "normal"));
        if (pj.contains("click_action")) {
            t.click_action = action_from_json(pj["click_action"]);
        }
        t.invert_x = pj.value("invert_x", false);
        t.invert_y = pj.value("invert_y", false);
        t.overlap = std::clamp(pj.value("overlap", 0.5f), 0.0f, 1.0f);
        if (pj.contains("radial_keys")) {
            auto& rk = pj["radial_keys"];
            for (size_t i = 0; i < 8 && i < rk.size(); i++) {
                t.radial_keys[i] = action_from_json(rk[i]);
            }
        }
    };
    if (j.contains("left_touchpad"))  parse_pad(j["left_touchpad"], p.left_touchpad);
    if (j.contains("right_touchpad")) parse_pad(j["right_touchpad"], p.right_touchpad);

    // Gyro
    if (j.contains("gyro")) {
        p.gyro.mode = parse_gyro_mode(j["gyro"].value("mode", "disabled"));
        p.gyro.sensitivity_x = std::clamp(j["gyro"].value("sensitivity_x", 1.0f), 0.1f, 20.0f);
        p.gyro.sensitivity_y = std::clamp(j["gyro"].value("sensitivity_y", 1.0f), 0.1f, 20.0f);
        p.gyro.invert_x = j["gyro"].value("invert_x", false);
        p.gyro.invert_y = j["gyro"].value("invert_y", false);
    }

    return p;
}

// --- Config manager ---

Config::Config() {
    wchar_t exe_path[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) != 0) {
        config_dir_ = std::filesystem::path(exe_path).parent_path();
    } else {
        config_dir_ = std::filesystem::current_path();
    }
    ensure_dirs();
}

void Config::ensure_dirs() const {
    std::error_code ec;
    std::filesystem::create_directories(profiles_dir(), ec);
    if (ec) {
        spdlog::warn("Failed to create profiles directory: {}", ec.message());
    }
}

std::filesystem::path Config::profile_path(const std::string& name) const {
    return profiles_dir() / (name + ".json");
}

bool Config::load_profile(const std::string& name) {
    // "Default" is not a stored profile — it is the hardcoded standard values.
    // Switching to it just resets to the built-in defaults, so it can never be
    // edited from the filesystem. Any stray Default.json is removed.
    if (is_default_name(name)) {
        Profile def;  // name defaults to "Default"
        {
            std::unique_lock lock(mutex_);
            profile_ = def;
        }
        persist_active(name);
        std::error_code ec;
        std::filesystem::remove(profile_path(kDefaultProfileName), ec);
        spdlog::info("Switched to built-in Default values");
        return true;
    }

    auto path = profile_path(name);
    std::ifstream file(path);
    if (!file.is_open()) {
        spdlog::warn("Profile '{}' not found at {}", name, path.string());
        return false;
    }

    try {
        std::string content((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
        auto p = profile_from_json(content);

        {
            std::unique_lock lock(mutex_);
            profile_ = std::move(p);
        }
        persist_active(name);
        spdlog::info("Profile '{}' loaded", name);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to parse profile '{}': {}", name, e.what());
        return false;
    }
}

bool Config::write_profile(const std::filesystem::path& path, const Profile& p) const {
    try {
        ensure_dirs();
        auto tmp_path = path;
        tmp_path += L".tmp";
        {
            std::ofstream file(tmp_path);
            if (!file.is_open()) {
                spdlog::error("Cannot write profile to {}", tmp_path.string());
                return false;
            }
            file << profile_to_json(p);
        }
        std::error_code ec;
        std::filesystem::rename(tmp_path, path, ec);
        if (ec) {
            spdlog::error("Cannot rename temp profile: {}", ec.message());
            std::filesystem::remove(tmp_path, ec);
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to save profile: {}", e.what());
        return false;
    }
}

bool Config::save_profile() const {
    Profile copy;
    std::filesystem::path path;
    {
        std::shared_lock lock(mutex_);
        // Default is immutable — never overwrite it on disk.
        if (is_default_name(profile_.name)) return true;
        copy = profile_;
        path = profile_path(profile_.name);
    }

    if (!write_profile(path, copy)) {
        spdlog::error("Failed to save profile '{}'", copy.name);
        return false;
    }
    spdlog::info("Profile '{}' saved", copy.name);
    return true;
}

Profile Config::get_profile() const {
    std::shared_lock lock(mutex_);
    return profile_;
}

void Config::set_profile(const Profile& profile) {
    std::unique_lock lock(mutex_);
    profile_ = profile;
}

std::vector<std::string> Config::list_profiles() const {
    std::vector<std::string> names;
    auto dir = profiles_dir();
    if (!std::filesystem::exists(dir)) return names;

    for (auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() == ".json") {
            names.push_back(entry.path().stem().string());
        }
    }
    return names;
}

std::string Config::active_name() const {
    std::shared_lock lock(mutex_);
    return profile_.name;
}

std::filesystem::path Config::active_marker_path() const {
    return profiles_dir() / ".active";
}

void Config::persist_active(const std::string& name) const {
    std::ofstream file(active_marker_path());
    if (file.is_open()) {
        file << name;
    } else {
        spdlog::warn("Cannot persist active-profile marker to {}", active_marker_path().string());
    }
}

std::string Config::persisted_active() const {
    std::ifstream file(active_marker_path());
    if (file.is_open()) {
        std::string name;
        std::getline(file, name);
        if (!name.empty()) return name;
    }
    return kDefaultProfileName;
}

bool Config::create_from_default(const std::string& name) {
    if (name.empty() || is_default_name(name)) {
        spdlog::warn("Refusing to create profile with reserved/empty name '{}'", name);
        return false;
    }
    Profile p;         // standard Default mapping
    p.name = name;
    {
        std::unique_lock lock(mutex_);
        profile_ = p;
    }
    if (!save_profile()) return false;
    persist_active(name);
    spdlog::info("Created profile '{}' from Default", name);
    return true;
}

bool Config::delete_profile(const std::string& name) {
    if (is_default_name(name)) {
        spdlog::warn("Refusing to delete the Default profile");
        return false;
    }
    std::error_code ec;
    std::filesystem::remove(profile_path(name), ec);
    if (ec) {
        spdlog::warn("Failed to delete profile '{}': {}", name, ec.message());
        return false;
    }
    // No concurrent profile writer can race this read-then-reset: delete runs
    // only on the input thread with name-entry off; the sole other writer
    // (end_name_entry on the overlay thread) is a no-op unless name_entry_ is set
    // (only by this same input thread); and Config's shared_mutex makes each
    // profile_ access atomic.
    bool was_active;
    {
        std::shared_lock lock(mutex_);
        was_active = (profile_.name == name);
    }
    if (was_active) load_profile(kDefaultProfileName);
    spdlog::info("Deleted profile '{}'", name);
    return true;
}

std::string Config::unique_name(const std::string& base) const {
    auto names = list_profiles();
    auto taken = [&](const std::string& n) {
        return is_default_name(n) ||
               std::find(names.begin(), names.end(), n) != names.end();
    };
    if (base.empty()) {
        for (int i = 1; i < 1000; i++) {
            std::string candidate = "Custom " + std::to_string(i);
            if (!taken(candidate)) return candidate;
        }
        return "Custom";
    }
    if (!taken(base)) return base;
    for (int i = 2; i < 1000; i++) {
        std::string candidate = base + " " + std::to_string(i);
        if (!taken(candidate)) return candidate;
    }
    return base;
}

} // namespace windeck::config
