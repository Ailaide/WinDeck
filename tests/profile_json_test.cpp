#include "config/config.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace windeck::config;

// Print profiles as their JSON in failure messages.
namespace Catch {
template <>
struct StringMaker<Profile> {
    static std::string convert(const Profile& p) { return profile_to_json(p); }
};
} // namespace Catch

namespace {

Profile round_trip(const Profile& p) {
    return profile_from_json(profile_to_json(p));
}

// Every field of every config struct differs from its default somewhere here.
Profile customized_profile() {
    Profile p;
    p.name = "Everything Changed";

    p.buttons[static_cast<size_t>(ButtonId::A)]         = {ActionType::Keyboard, 0x20};
    p.buttons[static_cast<size_t>(ButtonId::B)]         = {ActionType::MouseButton, 1};
    p.buttons[static_cast<size_t>(ButtonId::X)]         = {ActionType::MouseScroll, 1};
    p.buttons[static_cast<size_t>(ButtonId::Y)]         = {ActionType::None, 0};
    p.buttons[static_cast<size_t>(ButtonId::L4)]        = {ActionType::XboxButton, 0x1000};
    p.buttons[static_cast<size_t>(ButtonId::RPadClick)] = {ActionType::Keyboard, 0x0D};

    p.left_stick.mode = StickMode::Wasd;
    p.left_stick.inner_deadzone = 0.125f;
    p.left_stick.outer_deadzone = 0.875f;
    p.left_stick.curve = ResponseCurve::Quadratic;
    p.left_stick.mouse_sensitivity = 2.5f;
    p.left_stick.invert_x = true;
    p.left_stick.invert_y = true;
    p.left_stick.overlap = 0.25f;

    p.right_stick.mode = StickMode::Mouse;
    p.right_stick.curve = ResponseCurve::InverseQuadratic;
    p.right_stick.mouse_sensitivity = 7.0f;

    p.left_trigger.mode = TriggerMode::Buttons;
    p.left_trigger.deadzone = 0.1f;
    p.left_trigger.bindings[0] = {{ActionType::MouseButton, 1}, 0.25f, BindingMode::Hold, ClickStrength::None};
    p.left_trigger.bindings[1] = {{ActionType::Keyboard, 'E'}, 0.35f, BindingMode::Immediate, ClickStrength::High};
    p.left_trigger.bindings[2] = {{ActionType::XboxButton, 0x1000}, 1.0f, BindingMode::Hold, ClickStrength::Low};
    // Xbox mode keeps its bindings too.
    p.right_trigger.deadzone = 0.3f;
    p.right_trigger.bindings[1] = {{ActionType::MouseScroll, 1}, 0.55f, BindingMode::Immediate, ClickStrength::Low};

    p.dpad.mode = DpadMode::Arrows;
    p.dpad.overlap = 0.75f;

    p.left_touchpad.mode = TouchpadMode::RadialMenu;
    p.left_touchpad.radial_keys[0] = {ActionType::Keyboard, 'Q'};
    p.left_touchpad.radial_keys[7] = {ActionType::XboxButton, 0x0010};

    p.right_touchpad.mode = TouchpadMode::Mouse;
    p.right_touchpad.pressure_threshold = 12000;
    p.right_touchpad.sensitivity = 3.5f;
    p.right_touchpad.inertia = 0.8f;
    p.right_touchpad.acceleration = TouchpadAccelerationPreset::Strong;
    p.right_touchpad.click_action = {ActionType::MouseButton, 2};
    p.right_touchpad.invert_x = true;
    p.right_touchpad.invert_y = true;
    p.right_touchpad.overlap = 0.1f;

    p.gyro.mode = GyroMode::Mouse;
    p.gyro.sensitivity_x = 4.0f;
    p.gyro.sensitivity_y = 0.5f;
    p.gyro.invert_x = true;
    p.gyro.invert_y = true;

    return p;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("Default profile survives a JSON round trip", "[profile]") {
    Profile p;
    CHECK(round_trip(p) == p);
}

TEST_CASE("Every profile field survives a JSON round trip", "[profile]") {
    const Profile p = customized_profile();
    REQUIRE_FALSE(p == Profile{});

    // Per section first, so a failure names the part that lost data.
    const Profile rt = round_trip(p);
    CHECK(rt.name == p.name);
    CHECK(rt.buttons == p.buttons);
    CHECK(rt.left_stick == p.left_stick);
    CHECK(rt.right_stick == p.right_stick);
    CHECK(rt.left_trigger == p.left_trigger);
    CHECK(rt.right_trigger == p.right_trigger);
    CHECK(rt.dpad == p.dpad);
    CHECK(rt.left_touchpad == p.left_touchpad);
    CHECK(rt.right_touchpad == p.right_touchpad);
    CHECK(rt.gyro == p.gyro);
    CHECK(rt == p);
}

TEST_CASE("Serialized JSON is stable across round trips", "[profile]") {
    const std::string once = profile_to_json(customized_profile());
    CHECK(profile_to_json(profile_from_json(once)) == once);
}

TEST_CASE("Every touchpad mode survives a JSON round trip", "[profile]") {
    for (auto mode : {TouchpadMode::TrackpadInertia, TouchpadMode::Mouse, TouchpadMode::Wasd,
                      TouchpadMode::Arrows, TouchpadMode::RadialMenu, TouchpadMode::DpadButtons,
                      TouchpadMode::Scroll, TouchpadMode::Disabled}) {
        Profile p;
        p.right_touchpad.mode = mode;
        CAPTURE(static_cast<int>(mode));
        CHECK(round_trip(p).right_touchpad.mode == mode);
    }
}

TEST_CASE("Missing sections keep their defaults", "[profile]") {
    CHECK(profile_from_json("{}") == Profile{});

    Profile p = profile_from_json(R"({"name": "Sparse", "dpad": {"mode": "wasd"}})");
    CHECK(p.name == "Sparse");
    CHECK(p.dpad.mode == DpadMode::Wasd);
    CHECK(p.dpad.overlap == DpadConfig{}.overlap);
    CHECK(p.left_stick == StickConfig{});
    CHECK(p.buttons == Profile{}.buttons);
}

TEST_CASE("Out-of-range values are clamped on load", "[profile]") {
    Profile p = profile_from_json(R"({
        "left_stick":     {"inner_deadzone": -0.5, "outer_deadzone": 1.5,
                           "mouse_sensitivity": 0.0, "overlap": 2.0},
        "left_trigger":   {"deadzone": 3.0, "a": {"pull": -1.0}, "c": {"pull": 2.0}},
        "right_touchpad": {"sensitivity": 50.0, "inertia": 1.0},
        "left_touchpad":  {"inertia": 0.1},
        "gyro":           {"sensitivity_x": -1.0, "sensitivity_y": 100.0}
    })");

    CHECK(p.left_stick.inner_deadzone == 0.0f);
    CHECK(p.left_stick.outer_deadzone == 1.0f);
    CHECK(p.left_stick.mouse_sensitivity == 0.1f);
    CHECK(p.left_stick.overlap == 1.0f);
    CHECK(p.left_trigger.deadzone == kTriggerDeadzoneMax);
    CHECK(p.left_trigger.bindings[0].pull == kTriggerPullMin);
    CHECK(p.left_trigger.bindings[1] == TriggerConfig{}.bindings[1]);
    CHECK(p.left_trigger.bindings[2].pull == kTriggerPullMax);
    CHECK(p.right_touchpad.sensitivity == 20.0f);
    CHECK(p.right_touchpad.inertia == 0.999f);
    CHECK(p.left_touchpad.inertia == 0.5f);
    CHECK(p.gyro.sensitivity_x == 0.1f);
    CHECK(p.gyro.sensitivity_y == 20.0f);
}

TEST_CASE("Unknown names fall back to safe defaults", "[profile]") {
    Profile p = profile_from_json(R"({
        "buttons":    {"A": {"type": "laser", "code": 7}, "NotAButton": {"type": "xbox", "code": 1}},
        "left_stick": {"mode": "hover", "curve": "cubic"},
        "dpad":       {"mode": "joystick"},
        "gyro":       {"mode": "steering"},
        "right_trigger": {"mode": "turbo", "a": {"mode": "spam", "click": "loud"}}
    })");

    CHECK(p.buttons[static_cast<size_t>(ButtonId::A)] == ButtonAction{ActionType::None, 0});
    CHECK(p.left_stick.mode == StickMode::Stick);
    CHECK(p.left_stick.curve == ResponseCurve::Linear);
    CHECK(p.dpad.mode == DpadMode::Dpad);
    CHECK(p.gyro.mode == GyroMode::Disabled);
    CHECK(p.right_trigger.mode == TriggerMode::Xbox);
    CHECK(p.right_trigger.bindings[0].mode == BindingMode::Hold);
    CHECK(p.right_trigger.bindings[0].click == ClickStrength::Low);
}

TEST_CASE("Both trigger modes round-trip with all three bindings", "[profile]") {
    for (auto mode : {TriggerMode::Xbox, TriggerMode::Buttons}) {
        Profile p;
        p.left_trigger.mode = mode;
        p.left_trigger.bindings[2] = {{ActionType::Keyboard, 'Q'}, 0.45f, BindingMode::Immediate,
                                      ClickStrength::None};
        CAPTURE(static_cast<int>(mode));
        CHECK(round_trip(p).left_trigger == p.left_trigger);
    }
}

TEST_CASE("A new trigger has explicit None bindings at 30, 60 and 95 percent", "[profile]") {
    const TriggerConfig t;
    CHECK(t.mode == TriggerMode::Xbox);
    for (const auto& b : t.bindings) {
        CHECK(b.action == ButtonAction{ActionType::None, 0});
        CHECK(b.mode == BindingMode::Hold);
        CHECK(b.click == ClickStrength::Low);
    }
    CHECK(t.bindings[0].pull == 0.30f);
    CHECK(t.bindings[1].pull == 0.60f);
    CHECK(t.bindings[2].pull == 0.95f);
}

TEST_CASE("Missing trigger bindings keep their defaults; extra keys are ignored", "[profile]") {
    Profile p = profile_from_json(R"({
        "left_trigger": {"mode": "buttons",
                         "b": {"action": {"type": "keyboard", "code": 69}},
                         "d": {"pull": 0.5}}
    })");
    const TriggerConfig defaults;
    CHECK(p.left_trigger.mode == TriggerMode::Buttons);
    CHECK(p.left_trigger.bindings[0] == defaults.bindings[0]);
    CHECK(p.left_trigger.bindings[1].action == ButtonAction{ActionType::Keyboard, 69});
    CHECK(p.left_trigger.bindings[1].pull == defaults.bindings[1].pull);
    CHECK(p.left_trigger.bindings[2] == defaults.bindings[2]);
}

TEST_CASE("The old trigger keys are ignored", "[profile]") {
    Profile p = profile_from_json(R"({
        "left_trigger": {"threshold": 0.4,
                         "button": {"threshold": 0.8, "action": {"type": "keyboard", "code": 69}}}
    })");
    CHECK(p.left_trigger == TriggerConfig{});
}

TEST_CASE("A value of the wrong JSON type rejects the profile", "[profile]") {
    CHECK_THROWS(profile_from_json(R"({"left_trigger": []})"));
    CHECK_THROWS(profile_from_json(R"({"left_trigger": {"deadzone": "high"}})"));
    CHECK_THROWS(profile_from_json(R"({"left_trigger": {"deadzone": true}})"));
    CHECK_THROWS(profile_from_json(R"({"left_trigger": {"a": 5}})"));
    CHECK_THROWS(profile_from_json(R"({"left_trigger": {"a": {"pull": "deep"}}})"));
    CHECK_THROWS(profile_from_json(R"({"left_trigger": {"a": {"mode": 1}}})"));
    CHECK_THROWS(profile_from_json(R"({"buttons": {"A": 7}})"));
    CHECK_THROWS(profile_from_json(R"({"buttons": {"A": {"type": "keyboard", "code": "65"}}})"));
    CHECK_THROWS(profile_from_json(R"({"buttons": {"A": {"type": 3, "code": 65}}})"));
}

TEST_CASE("Action codes are checked before they are narrowed", "[profile]") {
    auto action = [](const char* type, const char* code) {
        std::string json = std::string(R"({"buttons": {"A": {"type": ")") + type +
                           R"(", "code": )" + code + "}}}";
        return profile_from_json(json).buttons[static_cast<size_t>(ButtonId::A)];
    };
    const ButtonAction none{ActionType::None, 0};

    CHECK(action("keyboard", "65") == ButtonAction{ActionType::Keyboard, 65});
    CHECK(action("keyboard", "65.0") == ButtonAction{ActionType::Keyboard, 65});
    CHECK(action("keyboard", "65601") == none);   // would wrap round to 65 ('A') in 16 bits
    CHECK(action("keyboard", "-1") == none);
    CHECK(action("keyboard", "65.5") == none);
    CHECK(action("keyboard", "0") == none);
    CHECK(action("keyboard", "256") == none);
    CHECK(action("mouse", "2") == ButtonAction{ActionType::MouseButton, 2});
    CHECK(action("mouse", "3") == none);
    CHECK(action("scroll", "1") == ButtonAction{ActionType::MouseScroll, 1});
    CHECK(action("scroll", "2") == none);
    CHECK(action("xbox", "12288") == ButtonAction{ActionType::XboxButton, 0x3000});   // A + B
    CHECK(action("xbox", "0") == none);
    CHECK(action("xbox", "2048") == none);    // 0x0800 is reserved
    CHECK(action("xbox", "6144") == none);    // A plus the reserved bit
    CHECK(action("none", "7") == none);
}

TEST_CASE("Malformed JSON is rejected", "[profile]") {
    CHECK_THROWS_AS(profile_from_json("{ \"name\": "), nlohmann::json::parse_error);
    CHECK_THROWS_AS(profile_from_json(""), nlohmann::json::parse_error);
}

TEST_CASE("Example profiles load and round-trip", "[profile]") {
    const std::filesystem::path dir = WINDECK_PROFILES_DIR;
    REQUIRE(std::filesystem::is_directory(dir));

    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".json") continue;
        ++count;
        CAPTURE(entry.path().filename().string());

        const Profile p = profile_from_json(read_file(entry.path()));
        // Config saves a profile to <name>.json, so the two must match.
        CHECK(p.name == entry.path().stem().string());
        CHECK(round_trip(p) == p);
    }
    CHECK(count > 0);
}
