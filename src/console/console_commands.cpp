#include "console/console_commands.h"
#include <algorithm>
#include <cstdio>
#include <sstream>

namespace windeck::console {

namespace cfg = windeck::config;

ConsoleCommands::ConsoleCommands(cfg::Config& config, windeck::mapper::Mapper& mapper,
                                 windeck::output::SyntheticInput& synth)
    : config_(config), mapper_(mapper), synth_(synth) {}

static bool parse_u16(const std::string& s, uint16_t& out) {
    try { out = static_cast<uint16_t>(std::stoul(s, nullptr, 0)); return true; }
    catch (...) { return false; }
}

static bool parse_action(const std::string& type_str, const std::string& code_str,
                          cfg::ButtonAction& out) {
    if (type_str == "none") { out = {cfg::ActionType::None, 0}; return true; }
    uint16_t code = 0;
    if (!parse_u16(code_str, code)) { printf("Invalid code: %s\n", code_str.c_str()); return false; }
    if      (type_str == "xbox")   out = {cfg::ActionType::XboxButton,  code};
    else if (type_str == "key")    out = {cfg::ActionType::Keyboard,    code};
    else if (type_str == "mouse")  out = {cfg::ActionType::MouseButton, code};
    else if (type_str == "scroll") out = {cfg::ActionType::MouseScroll, code};
    else { printf("Unknown type: %s (xbox|key|mouse|scroll|none)\n", type_str.c_str()); return false; }
    return true;
}

void ConsoleCommands::process(const std::string& line) {
    auto profile = config_.get_profile();
    bool changed = false;
    std::istringstream ss(line);
    std::string cmd; ss >> cmd;

    if (cmd == "help") {
        printf("Modes:\n");
        printf("  rpad|lpad trackpad|mouse|wasd|arrows|radial|scroll|disabled\n");
        printf("  lstick|rstick stick|wasd|arrows|mouse\n");
        printf("  dpad dpad|wasd|arrows\n");
        printf("  gyro disabled|mouse\n");
        printf("Buttons:\n");
        printf("  button <name> xbox|key|mouse <code>   (name: A B X Y LB RB Start Back LS RS L4 L5 R4 R5 LPadClick RPadClick)\n");
        printf("  button <name> scroll 1|0              (1=scroll up, 0=scroll down, one tick per press)\n");
        printf("  button <name> none\n");
        printf("  Xbox codes: A=0x1000 B=0x2000 X=0x4000 Y=0x8000 LB=0x100 RB=0x200 Start=0x10 Back=0x20 LS=0x40 RS=0x80\n");
        printf("Triggers:\n");
        printf("  ltrigger|rtrigger threshold <0.0-1.0>\n");
        printf("  ltrigger|rtrigger button key|mouse <vk> [btn_threshold]\n");
        printf("  ltrigger|rtrigger button none\n");
        printf("Stick params:\n");
        printf("  lstick|rstick inner_deadzone|outer_deadzone <0.0-1.0>\n");
        printf("  lstick|rstick curve linear|quadratic|inverse_quadratic\n");
        printf("  lstick|rstick mouse_sensitivity <value>\n");
        printf("Pad params:\n");
        printf("  lpad|rpad sensitivity|inertia <value>\n");
        printf("  lpad|rpad pressure <0-32767>\n");
        printf("  lpad|rpad acceleration off|normal|strong\n");
        printf("  lpad|rpad click xbox|key|mouse <code>\n");
        printf("  lpad|rpad click none\n");
        printf("Global params:\n");
        printf("  overlap <0.0-1.0>\n");
        printf("Gyro params:\n");
        printf("  gyro sensitivity_x|sensitivity_y <value>\n");
        printf("Other:\n");
        printf("  load <profile_name>  - load and apply profile from disk\n");
        printf("  name <profile_name>  - rename current profile\n");
        printf("  save                 - save current profile\n");
        printf("  status               - show current config\n");

    } else if (cmd == "rpad" || cmd == "lpad") {
        bool is_right = (cmd[0] == 'r');
        auto& pad = is_right ? profile.right_touchpad : profile.left_touchpad;
        std::string sub; ss >> sub;
        if      (sub == "trackpad")    { pad.mode = cfg::TouchpadMode::TrackpadInertia; changed = true; printf("%cpad -> trackpad\n",  cmd[0]); }
        else if (sub == "mouse")       { pad.mode = cfg::TouchpadMode::Mouse;           changed = true; printf("%cpad -> mouse\n",     cmd[0]); }
        else if (sub == "wasd")        { pad.mode = cfg::TouchpadMode::Wasd;            changed = true; printf("%cpad -> wasd\n",      cmd[0]); }
        else if (sub == "arrows")      { pad.mode = cfg::TouchpadMode::Arrows;          changed = true; printf("%cpad -> arrows\n",    cmd[0]); }
        else if (sub == "scroll")      { pad.mode = cfg::TouchpadMode::Scroll;          changed = true; printf("%cpad -> scroll\n",    cmd[0]); }
        else if (sub == "radial")      { pad.mode = cfg::TouchpadMode::RadialMenu;      changed = true; printf("%cpad -> radial\n",   cmd[0]); }
        else if (sub == "disabled")    { pad.mode = cfg::TouchpadMode::Disabled;        changed = true; printf("%cpad -> disabled\n",  cmd[0]); }
        else if (sub == "sensitivity") {
            float v; if (!(ss >> v)) { printf("Missing value\n"); return; }
            pad.sensitivity = std::clamp(v, 0.1f, 20.0f); changed = true;
            printf("%cpad sensitivity -> %.2f\n", cmd[0], pad.sensitivity);
        } else if (sub == "inertia") {
            float v; if (!(ss >> v)) { printf("Missing value\n"); return; }
            pad.inertia = std::clamp(v, 0.5f, 0.999f); changed = true;
            printf("%cpad friction -> %.3f\n", cmd[0], pad.inertia);
        } else if (sub == "pressure") {
            uint16_t v; std::string vs; ss >> vs;
            if (!parse_u16(vs, v)) { printf("Invalid value\n"); return; }
            pad.pressure_threshold = v; changed = true;
            printf("%cpad pressure -> %u\n", cmd[0], pad.pressure_threshold);
        } else if (sub == "acceleration") {
            std::string a; ss >> a;
            if      (a == "off")    { pad.acceleration = cfg::TouchpadAccelerationPreset::Off;    changed = true; }
            else if (a == "normal") { pad.acceleration = cfg::TouchpadAccelerationPreset::Normal; changed = true; }
            else if (a == "strong") { pad.acceleration = cfg::TouchpadAccelerationPreset::Strong; changed = true; }
            else { printf("Unknown: %s (off|normal|strong)\n", a.c_str()); return; }
            printf("%cpad acceleration -> %s\n", cmd[0], a.c_str());
        } else if (sub == "click") {
            std::string type_str, code_str; ss >> type_str >> code_str;
            cfg::ButtonAction action;
            if (!parse_action(type_str, code_str, action)) return;
            pad.click_action = action; changed = true;
            printf("%cpad click -> %s %s\n", cmd[0], type_str.c_str(), code_str.c_str());
        } else { printf("Unknown: %s\n", sub.c_str()); return; }

    } else if (cmd == "lstick" || cmd == "rstick") {
        bool is_right = (cmd[0] == 'r');
        auto& stick = is_right ? profile.right_stick : profile.left_stick;
        std::string sub; ss >> sub;
        if      (sub == "stick")   { stick.mode = cfg::StickMode::Stick;  changed = true; printf("%cstick -> stick\n",  cmd[0]); }
        else if (sub == "wasd")    { stick.mode = cfg::StickMode::Wasd;   changed = true; printf("%cstick -> wasd\n",   cmd[0]); }
        else if (sub == "arrows")  { stick.mode = cfg::StickMode::Arrows; changed = true; printf("%cstick -> arrows\n", cmd[0]); }
        else if (sub == "mouse")   { stick.mode = cfg::StickMode::Mouse;  changed = true; printf("%cstick -> mouse\n",  cmd[0]); }
        else if (sub == "inner_deadzone") {
            float v; if (!(ss >> v)) { printf("Missing value\n"); return; }
            stick.inner_deadzone = std::clamp(v, 0.0f, 1.0f); changed = true;
            printf("%cstick inner_deadzone -> %.3f\n", cmd[0], stick.inner_deadzone);
        } else if (sub == "outer_deadzone") {
            float v; if (!(ss >> v)) { printf("Missing value\n"); return; }
            stick.outer_deadzone = std::clamp(v, 0.0f, 1.0f); changed = true;
            printf("%cstick outer_deadzone -> %.3f\n", cmd[0], stick.outer_deadzone);
        } else if (sub == "curve") {
            std::string c; ss >> c;
            if      (c == "linear")           { stick.curve = cfg::ResponseCurve::Linear;           changed = true; }
            else if (c == "quadratic")         { stick.curve = cfg::ResponseCurve::Quadratic;        changed = true; }
            else if (c == "inverse_quadratic") { stick.curve = cfg::ResponseCurve::InverseQuadratic; changed = true; }
            else { printf("Unknown: %s (linear|quadratic|inverse_quadratic)\n", c.c_str()); return; }
            printf("%cstick curve -> %s\n", cmd[0], c.c_str());
        } else if (sub == "mouse_sensitivity") {
            float v; if (!(ss >> v)) { printf("Missing value\n"); return; }
            stick.mouse_sensitivity = std::clamp(v, 0.1f, 20.0f); changed = true;
            printf("%cstick mouse_sensitivity -> %.2f\n", cmd[0], stick.mouse_sensitivity);
        } else { printf("Unknown: %s\n", sub.c_str()); return; }

    } else if (cmd == "overlap") {
        float v; if (!(ss >> v)) { printf("Missing value\n"); return; }
        profile.overlap = std::clamp(v, 0.0f, 1.0f); changed = true;
        printf("overlap -> %.2f\n", profile.overlap);

    } else if (cmd == "dpad") {
        std::string sub; ss >> sub;
        if      (sub == "dpad")   { profile.dpad.mode = cfg::DpadMode::Dpad;   changed = true; printf("dpad -> dpad\n"); }
        else if (sub == "wasd")   { profile.dpad.mode = cfg::DpadMode::Wasd;   changed = true; printf("dpad -> wasd\n"); }
        else if (sub == "arrows") { profile.dpad.mode = cfg::DpadMode::Arrows; changed = true; printf("dpad -> arrows\n"); }
        else { printf("Unknown: %s\n", sub.c_str()); return; }

    } else if (cmd == "gyro") {
        std::string sub; ss >> sub;
        if      (sub == "disabled") { profile.gyro.mode = cfg::GyroMode::Disabled; changed = true; printf("gyro -> disabled\n"); }
        else if (sub == "mouse")    { profile.gyro.mode = cfg::GyroMode::Mouse;    changed = true; printf("gyro -> mouse\n"); }
        else if (sub == "sensitivity_x") {
            float v; if (!(ss >> v)) { printf("Missing value\n"); return; }
            profile.gyro.sensitivity_x = std::clamp(v, 0.1f, 20.0f); changed = true;
            printf("gyro sensitivity_x -> %.2f\n", profile.gyro.sensitivity_x);
        } else if (sub == "sensitivity_y") {
            float v; if (!(ss >> v)) { printf("Missing value\n"); return; }
            profile.gyro.sensitivity_y = std::clamp(v, 0.1f, 20.0f); changed = true;
            printf("gyro sensitivity_y -> %.2f\n", profile.gyro.sensitivity_y);
        } else { printf("Unknown: %s\n", sub.c_str()); return; }

    } else if (cmd == "button") {
        static const char* btn_names[] = {
            "A","B","X","Y","LB","RB","Start","Back",
            "LS","RS","L4","L5","R4","R5","LPadClick","RPadClick"
        };
        std::string btn_name, type_str, code_str;
        ss >> btn_name >> type_str >> code_str;
        int btn_idx = -1;
        for (int i = 0; i < 16; i++) {
            if (btn_name == btn_names[i]) { btn_idx = i; break; }
        }
        if (btn_idx < 0) { printf("Unknown button: %s\n", btn_name.c_str()); return; }
        cfg::ButtonAction action;
        if (!parse_action(type_str, code_str, action)) return;
        profile.buttons[btn_idx] = action;
        changed = true;
        printf("button %s -> %s %s\n", btn_name.c_str(), type_str.c_str(), code_str.c_str());

    } else if (cmd == "ltrigger" || cmd == "rtrigger") {
        bool is_right = (cmd[0] == 'r');
        auto& trig = is_right ? profile.right_trigger : profile.left_trigger;
        std::string sub; ss >> sub;
        if (sub == "threshold") {
            float v; if (!(ss >> v)) { printf("Missing value\n"); return; }
            trig.threshold = std::clamp(v, 0.0f, 1.0f); changed = true;
            printf("%ctrigger threshold -> %.3f\n", cmd[0], trig.threshold);
        } else if (sub == "button") {
            std::string type_str; ss >> type_str;
            if (type_str == "none") {
                trig.has_button = false; changed = true;
                printf("%ctrigger button -> none\n", cmd[0]);
            } else if (type_str == "key" || type_str == "mouse") {
                std::string code_str; ss >> code_str;
                float btn_thresh = 0.8f; ss >> btn_thresh;
                uint16_t code = 0;
                if (!parse_u16(code_str, code)) { printf("Invalid code: %s\n", code_str.c_str()); return; }
                trig.has_button = true;
                trig.button_action.type = (type_str == "key") ? cfg::ActionType::Keyboard
                                                               : cfg::ActionType::MouseButton;
                trig.button_action.code = code;
                trig.button_threshold = std::clamp(btn_thresh, 0.0f, 1.0f);
                changed = true;
                printf("%ctrigger button -> %s %u at %.2f\n",
                    cmd[0], type_str.c_str(), code, trig.button_threshold);
            } else { printf("Unknown type: %s (key|mouse|none)\n", type_str.c_str()); return; }
        } else { printf("Unknown: %s (threshold|button)\n", sub.c_str()); return; }

    } else if (cmd == "load") {
        std::string profile_name; ss >> profile_name;
        if (profile_name.empty()) { printf("Usage: load <profile_name>\n"); return; }
        if (!config_.load_profile(profile_name)) {
            printf("Profile '%s' not found.\n", profile_name.c_str());
            return;
        }
        auto loaded = config_.get_profile();
        mapper_.apply_profile(loaded, synth_);
        printf("Profile '%s' loaded and applied.\n", profile_name.c_str());
        return;

    } else if (cmd == "name") {
        std::string new_name; ss >> new_name;
        if (new_name.empty()) { printf("Usage: name <profile_name>\n"); return; }
        profile.name = new_name; changed = true;
        printf("profile name -> %s\n", new_name.c_str());

    } else if (cmd == "save") {
        config_.save_profile();
        printf("Profile saved.\n");

    } else if (cmd == "status") {
        auto p = config_.get_profile();
        const char* pad_modes[]   = {"trackpad","mouse","wasd","arrows","radial","dpad","scroll","disabled"};
        const char* stick_modes[] = {"stick","wasd","arrows","mouse"};
        const char* dpad_modes[]  = {"dpad","wasd","arrows"};
        const char* gyro_modes[]  = {"disabled","mouse"};
        const char* accel[]       = {"off","normal","strong"};
        const char* curve[]       = {"linear","quadratic","inverse_quadratic"};
        auto s = [](const char* arr[], size_t n, int i) -> const char* {
            return (i >= 0 && static_cast<size_t>(i) < n) ? arr[i] : "?";
        };
        printf("rpad=%s lpad=%s\n",
            s(pad_modes,8,static_cast<int>(p.right_touchpad.mode)),
            s(pad_modes,8,static_cast<int>(p.left_touchpad.mode)));
        printf("  rpad: sensitivity=%.2f friction=%.3f pressure=%u accel=%s\n",
            p.right_touchpad.sensitivity, p.right_touchpad.inertia,
            p.right_touchpad.pressure_threshold,
            s(accel,3,static_cast<int>(p.right_touchpad.acceleration)));
        printf("  lpad: sensitivity=%.2f friction=%.3f pressure=%u accel=%s\n",
            p.left_touchpad.sensitivity, p.left_touchpad.inertia,
            p.left_touchpad.pressure_threshold,
            s(accel,3,static_cast<int>(p.left_touchpad.acceleration)));
        printf("lstick=%s rstick=%s\n",
            s(stick_modes,4,static_cast<int>(p.left_stick.mode)),
            s(stick_modes,4,static_cast<int>(p.right_stick.mode)));
        printf("  lstick: inner=%.2f outer=%.2f curve=%s mouse_sens=%.2f\n",
            p.left_stick.inner_deadzone, p.left_stick.outer_deadzone,
            s(curve,3,static_cast<int>(p.left_stick.curve)),
            p.left_stick.mouse_sensitivity);
        printf("  rstick: inner=%.2f outer=%.2f curve=%s mouse_sens=%.2f\n",
            p.right_stick.inner_deadzone, p.right_stick.outer_deadzone,
            s(curve,3,static_cast<int>(p.right_stick.curve)),
            p.right_stick.mouse_sensitivity);
        printf("dpad=%s  gyro=%s sens_x=%.2f sens_y=%.2f\n",
            s(dpad_modes,3,static_cast<int>(p.dpad.mode)),
            s(gyro_modes,2,static_cast<int>(p.gyro.mode)),
            p.gyro.sensitivity_x, p.gyro.sensitivity_y);
        printf("overlap=%.2f\n", p.overlap);
        printf("ltrigger: threshold=%.3f  rtrigger: threshold=%.3f\n",
            p.left_trigger.threshold, p.right_trigger.threshold);

    } else {
        printf("Unknown command. Type 'help'.\n");
    }

    if (changed) {
        config_.set_profile(profile);
        mapper_.apply_profile(profile, synth_);
    }
}

} // namespace windeck::console
