#include "config/config.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <shlobj.h>

using json = nlohmann::json;

namespace windeck::config {

// --- JSON helpers ---

static const char* action_type_str(ActionType t) {
    switch (t) {
        case ActionType::XboxButton:  return "xbox";
        case ActionType::Keyboard:    return "keyboard";
        case ActionType::MouseButton: return "mouse";
        default:                      return "none";
    }
}

static ActionType parse_action_type(const std::string& s) {
    if (s == "xbox")     return ActionType::XboxButton;
    if (s == "keyboard") return ActionType::Keyboard;
    if (s == "mouse")    return ActionType::MouseButton;
    return ActionType::None;
}

static json action_to_json(const ButtonAction& a) {
    return {{"type", action_type_str(a.type)}, {"code", a.code}};
}

static ButtonAction action_from_json(const json& j) {
    ButtonAction a;
    a.type = parse_action_type(j.value("type", "none"));
    a.code = j.value("code", (uint16_t)0);
    return a;
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

static const char* gyro_mode_str(GyroMode m) {
    switch (m) {
        case GyroMode::RightStick: return "right_stick";
        case GyroMode::Mouse:      return "mouse";
        default:                   return "off";
    }
}

static GyroMode parse_gyro_mode(const std::string& s) {
    if (s == "right_stick") return GyroMode::RightStick;
    if (s == "mouse")       return GyroMode::Mouse;
    return GyroMode::Off;
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
            {"overlap", s.overlap}
        };
    };
    j["left_stick"] = stick_json(p.left_stick);
    j["right_stick"] = stick_json(p.right_stick);

    // Triggers
    auto trig_json = [](const TriggerConfig& t) -> json {
        json tj = {{"threshold", t.threshold}};
        if (t.has_button) {
            tj["button"] = {
                {"threshold", t.button_threshold},
                {"action", action_to_json(t.button_action)}
            };
        }
        return tj;
    };
    j["left_trigger"] = trig_json(p.left_trigger);
    j["right_trigger"] = trig_json(p.right_trigger);

    // D-Pad
    j["dpad"] = {{"mode", dpad_mode_str(p.dpad.mode)}};

    // Touchpads
    auto pad_json = [](const TouchpadConfig& t) -> json {
        json pj = {
            {"mode", touchpad_mode_str(t.mode)},
            {"pressure_threshold", t.pressure_threshold},
            {"sensitivity", t.sensitivity},
            {"friction", t.friction},
            {"acceleration", touchpad_accel_str(t.acceleration)},
            {"click_action", action_to_json(t.click_action)}
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
    auto gyro_activation_str = [](GyroActivation a) -> std::string {
        switch (a) {
            case GyroActivation::TouchRightPad: return "touch_right_pad";
            case GyroActivation::TouchLeftPad:  return "touch_left_pad";
            default: return "always";
        }
    };
    j["gyro"] = {
        {"mode", gyro_mode_str(p.gyro.mode)},
        {"sensitivity_x", p.gyro.sensitivity_x},
        {"sensitivity_y", p.gyro.sensitivity_y},
        {"activation", gyro_activation_str(p.gyro.activation)}
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
        s.inner_deadzone = sj.value("inner_deadzone", 0.05f);
        s.outer_deadzone = sj.value("outer_deadzone", 0.95f);
        s.curve = parse_curve(sj.value("curve", "linear"));
        s.mouse_sensitivity = sj.value("mouse_sensitivity", 1.0f);
        s.overlap = sj.value("overlap", 0.0f);
    };
    if (j.contains("left_stick"))  parse_stick(j["left_stick"], p.left_stick);
    if (j.contains("right_stick")) parse_stick(j["right_stick"], p.right_stick);

    // Triggers
    auto parse_trig = [](const json& tj, TriggerConfig& t) {
        t.threshold = tj.value("threshold", 0.02f);
        if (tj.contains("button")) {
            t.has_button = true;
            t.button_threshold = tj["button"].value("threshold", 0.8f);
            if (tj["button"].contains("action")) {
                t.button_action = action_from_json(tj["button"]["action"]);
            }
        }
    };
    if (j.contains("left_trigger"))  parse_trig(j["left_trigger"], p.left_trigger);
    if (j.contains("right_trigger")) parse_trig(j["right_trigger"], p.right_trigger);

    // D-Pad
    if (j.contains("dpad")) {
        p.dpad.mode = parse_dpad_mode(j["dpad"].value("mode", "dpad"));
    }

    // Touchpads
    auto parse_pad = [](const json& pj, TouchpadConfig& t) {
        t.mode = parse_touchpad_mode(pj.value("mode", "trackpad_inertia"));
        t.pressure_threshold = pj.value("pressure_threshold", (uint16_t)50);
        t.sensitivity = pj.value("sensitivity", 1.0f);
        t.friction = pj.value("friction", 0.95f);
        t.acceleration = parse_touchpad_accel(pj.value("acceleration", "normal"));
        if (pj.contains("click_action")) {
            t.click_action = action_from_json(pj["click_action"]);
        }
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
        p.gyro.mode = parse_gyro_mode(j["gyro"].value("mode", "off"));
        p.gyro.sensitivity_x = j["gyro"].value("sensitivity_x", 1.0f);
        p.gyro.sensitivity_y = j["gyro"].value("sensitivity_y", 1.0f);
        auto act_str = j["gyro"].value("activation", "always");
        if (act_str == "touch_right_pad")     p.gyro.activation = GyroActivation::TouchRightPad;
        else if (act_str == "touch_left_pad") p.gyro.activation = GyroActivation::TouchLeftPad;
        else                                  p.gyro.activation = GyroActivation::Always;
    }

    return p;
}

// --- Config manager ---

Config::Config() {
    wchar_t* appdata = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata))) {
        config_dir_ = std::filesystem::path(appdata) / "WinDeck";
        CoTaskMemFree(appdata);
    } else {
        config_dir_ = std::filesystem::path(".") / "WinDeck";
    }
    ensure_dirs();
}

void Config::ensure_dirs() const {
    std::filesystem::create_directories(config_dir_ / "profiles");
}

std::filesystem::path Config::profile_path(const std::string& name) const {
    return config_dir_ / "profiles" / (name + ".json");
}

bool Config::load_profile(const std::string& name) {
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

        std::unique_lock lock(mutex_);
        profile_ = std::move(p);
        spdlog::info("Profile '{}' loaded", name);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to parse profile '{}': {}", name, e.what());
        return false;
    }
}

bool Config::save_profile() const {
    Profile copy;
    std::filesystem::path path;
    {
        std::shared_lock lock(mutex_);
        copy = profile_;
        path = profile_path(profile_.name);
    }

    try {
        ensure_dirs();
        std::ofstream file(path);
        if (!file.is_open()) {
            spdlog::error("Cannot write profile to {}", path.string());
            return false;
        }
        file << profile_to_json(copy);
        spdlog::info("Profile '{}' saved", copy.name);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to save profile: {}", e.what());
        return false;
    }
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
    auto dir = config_dir_ / "profiles";
    if (!std::filesystem::exists(dir)) return names;

    for (auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() == ".json") {
            names.push_back(entry.path().stem().string());
        }
    }
    return names;
}

} // namespace windeck::config
