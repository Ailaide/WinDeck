#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <UIAutomation.h>
#pragma comment(lib, "ole32.lib")

#include "neptune/neptune_device.h"
#include "output/virtual_xbox.h"
#include "output/synthetic_input.h"
#include "mapper/mapper.h"
#include "config/config.h"
#include "hidhide/hidhide_client.h"
#include "driver/driver_manager.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>
#include <chrono>
#include <filesystem>

static std::atomic<bool> g_running{true};
static std::atomic<bool> g_emergency_restore{false};
static std::atomic<bool> g_show_console{false};
static windeck::neptune::NeptuneDevice* g_neptune = nullptr;

static void signal_handler(int /*sig*/) {
    g_running.store(false, std::memory_order_relaxed);
}

static BOOL WINAPI console_handler(DWORD event) {
    switch (event) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
            g_running.store(false, std::memory_order_relaxed);
            Sleep(3000);
            return TRUE;
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            // Signal emergency restore. The input loop checks this flag
            // and performs the restore from its own thread (safe).
            g_emergency_restore.store(true, std::memory_order_relaxed);
            g_running.store(false, std::memory_order_relaxed);
            Sleep(5000); // Give input thread time to restore
            return TRUE;
    }
    return FALSE;
}

static void open_touch_keyboard() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    IUIAutomation* uia = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(CUIAutomation), nullptr,
        CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), reinterpret_cast<void**>(&uia));
    if (FAILED(hr) || !uia) {
        spdlog::warn("Failed to create UIAutomation: {:#x}", static_cast<unsigned>(hr));
        CoUninitialize();
        return;
    }

    // Find the system tray notification area
    HWND tray_wnd = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!tray_wnd) {
        spdlog::warn("Shell_TrayWnd not found");
        uia->Release();
        CoUninitialize();
        return;
    }

    IUIAutomationElement* tray_elem = nullptr;
    hr = uia->ElementFromHandle(tray_wnd, &tray_elem);
    if (FAILED(hr) || !tray_elem) {
        spdlog::warn("ElementFromHandle failed: {:#x}", static_cast<unsigned>(hr));
        uia->Release();
        CoUninitialize();
        return;
    }

    // Search for a button with AutomationId "TipBandButtonID" (the touch keyboard tray button)
    IUIAutomationCondition* cond_id = nullptr;
    VARIANT var_id;
    var_id.vt = VT_BSTR;
    var_id.bstrVal = SysAllocString(L"TipBandButtonID");
    uia->CreatePropertyCondition(UIA_AutomationIdPropertyId, var_id, &cond_id);
    SysFreeString(var_id.bstrVal);

    IUIAutomationElement* kbd_btn = nullptr;
    if (cond_id) {
        hr = tray_elem->FindFirst(TreeScope_Descendants, cond_id, &kbd_btn);
        cond_id->Release();
    }

    if (!kbd_btn) {
        // Fallback: search by Name containing "keyboard" (localization-safe via partial match)
        IUIAutomationCondition* cond_type = nullptr;
        VARIANT var_type;
        var_type.vt = VT_I4;
        var_type.lVal = UIA_ButtonControlTypeId;
        uia->CreatePropertyCondition(UIA_ControlTypePropertyId, var_type, &cond_type);

        IUIAutomationElementArray* buttons = nullptr;
        if (cond_type) {
            tray_elem->FindAll(TreeScope_Descendants, cond_type, &buttons);
            cond_type->Release();
        }

        if (buttons) {
            int count = 0;
            buttons->get_Length(&count);
            for (int i = 0; i < count && !kbd_btn; i++) {
                IUIAutomationElement* elem = nullptr;
                buttons->GetElement(i, &elem);
                if (elem) {
                    BSTR name = nullptr;
                    elem->get_CurrentName(&name);
                    if (name) {
                        std::wstring wname(name);
                        // Check common names across localizations
                        if (wname.find(L"keyboard") != std::wstring::npos ||
                            wname.find(L"Keyboard") != std::wstring::npos ||
                            wname.find(L"Touch") != std::wstring::npos) {
                            kbd_btn = elem;
                        }
                        SysFreeString(name);
                    }
                    if (kbd_btn != elem) elem->Release();
                }
            }
            buttons->Release();
        }
    }

    if (kbd_btn) {
        // Try TogglePattern first (toggle button), then InvokePattern as fallback
        IUIAutomationTogglePattern* toggle = nullptr;
        hr = kbd_btn->GetCurrentPatternAs(UIA_TogglePatternId,
            __uuidof(IUIAutomationTogglePattern), reinterpret_cast<void**>(&toggle));
        if (SUCCEEDED(hr) && toggle) {
            hr = toggle->Toggle();
            spdlog::info("Touch keyboard toggled via TogglePattern (hr={:#x})",
                static_cast<unsigned>(hr));
            toggle->Release();
        } else {
            IUIAutomationInvokePattern* invoke = nullptr;
            hr = kbd_btn->GetCurrentPatternAs(UIA_InvokePatternId,
                __uuidof(IUIAutomationInvokePattern), reinterpret_cast<void**>(&invoke));
            if (SUCCEEDED(hr) && invoke) {
                hr = invoke->Invoke();
                spdlog::info("Touch keyboard toggled via InvokePattern (hr={:#x})",
                    static_cast<unsigned>(hr));
                invoke->Release();
            } else {
                spdlog::warn("No Toggle or Invoke pattern on keyboard button: {:#x}",
                    static_cast<unsigned>(hr));
            }
        }
        kbd_btn->Release();
    } else {
        spdlog::warn("Touch keyboard button not found in taskbar. "
            "Enable it: Settings > Personalization > Taskbar > Touch keyboard");
    }

    tray_elem->Release();
    uia->Release();
    CoUninitialize();
}

static void input_loop(windeck::neptune::NeptuneDevice& device,
                        windeck::output::VirtualXbox& xbox,
                        windeck::mapper::Mapper& mapper,
                        windeck::output::SyntheticInput& synth) {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif

    windeck::neptune::InputReport input{};
    uint64_t error_count = 0;
    constexpr int kMaxConsecutiveErrors = 50;

    spdlog::info("Input loop started");

    while (g_running.load(std::memory_order_relaxed)) {
        int read_result = device.read(input);

        if (read_result > 0) {
            error_count = 0;

            // Map and send to virtual Xbox + keyboard/mouse
            auto result = mapper.map(input, synth);
            mapper.tick(synth);  // Tick inertia every frame, not just on timeout
            xbox.update(result.xbox_report);
            synth.flush();

            // Rumble passthrough (time-throttled, frame-independent)
            static auto next_rumble_send = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            if (now >= next_rumble_send) {
                next_rumble_send = now + std::chrono::milliseconds(32);
                auto rumble = xbox.get_rumble();
                uint16_t left  = static_cast<uint16_t>(rumble.right_motor) * 257;
                uint16_t right = static_cast<uint16_t>(rumble.left_motor) * 257;
                device.send_rumble(left, right);
            }

            // System buttons
            if (result.sys_events.steam_pressed) {
                g_show_console.store(true, std::memory_order_relaxed);
            }
            if (result.sys_events.quick_access_pressed) {
                spdlog::info("Quick Access button pressed, toggling touch keyboard");
                std::thread(open_touch_keyboard).detach();
            }
        } else if (read_result == 0) {
            // Timeout — tick inertia
            mapper.tick(synth);
            synth.flush();
        } else {
            error_count++;
            if (error_count == 1) {
                spdlog::warn("HID read issue detected, monitoring...");
            }
            if (error_count >= kMaxConsecutiveErrors) {
                spdlog::error("Too many consecutive HID errors, attempting reconnect...");
                mapper.release_all(synth);
                synth.release_all();
                synth.flush();
                device.close();
                std::this_thread::sleep_for(std::chrono::seconds(1));
                if (device.open()) {
                    spdlog::info("Neptune controller reconnected");
                    error_count = 0;
                } else {
                    spdlog::error("Reconnect failed, retrying in 3 seconds...");
                    std::this_thread::sleep_for(std::chrono::seconds(3));
                    error_count = 0;
                }
            }
        }
    }

    mapper.release_all(synth);
    synth.release_all();

    // Emergency restore: if console is closing, restore lizard mode from this thread
    // (safe — no other thread is accessing the device now)
    if (g_emergency_restore.load(std::memory_order_relaxed)) {
        device.enable_lizard_mode();
        spdlog::info("Emergency lizard mode restore completed");
        spdlog::default_logger()->flush();
    }

    spdlog::info("Input loop stopped");
}

static std::filesystem::path get_exe_dir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

static void setup_logging() {
    try {
        auto exe_dir = get_exe_dir();
        auto log_path = (exe_dir / "windeck.log").string();

        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_path, true);

        auto logger = std::make_shared<spdlog::logger>("windeck",
            spdlog::sinks_init_list{console_sink, file_sink});
        logger->set_level(spdlog::level::info);
        logger->flush_on(spdlog::level::warn);

        spdlog::set_default_logger(logger);
    } catch (const std::exception& e) {
        spdlog::set_level(spdlog::level::debug);
        spdlog::warn("Failed to create log file: {}", e.what());
    }
}

int main() {
    // Single-instance: prevent multiple copies
    HANDLE single_mutex = CreateMutexW(nullptr, TRUE, L"Global\\WinDeck_SingleInstance");
    if (!single_mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"WinDeck is already running.", L"WinDeck", MB_OK | MB_ICONINFORMATION);
        if (single_mutex) CloseHandle(single_mutex);
        return 0;
    }

    setup_logging();
    spdlog::info("WinDeck v0.2.0 starting");

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    SetConsoleCtrlHandler(console_handler, TRUE);

    // Ensure ViGEmBus driver is installed
    if (!windeck::driver::ensure_vigembus_installed()) {
        spdlog::error("Cannot proceed without ViGEmBus driver");
        return 1;
    }

    // Connect virtual Xbox 360 controller
    windeck::output::VirtualXbox xbox;
    if (!xbox.connect()) {
        spdlog::error("Failed to connect to ViGEmBus");
        return 1;
    }

    // Open Neptune controller
    windeck::neptune::NeptuneDevice neptune;
    if (!neptune.open()) {
        spdlog::error("Cannot proceed without Neptune controller");
        return 1;
    }
    g_neptune = &neptune;

    // HidHide disabled for now — will be re-enabled when UI is available
    // to manage it properly. Auto-install + hide without reliable restore
    // causes lockout if the app crashes or exits abnormally.

    // Load config and apply profile
    windeck::config::Config config;
    windeck::mapper::Mapper mapper;
    windeck::output::SyntheticInput synth;

    mapper.set_device(&neptune);

    // Try loading saved profile, fall back to defaults
    if (!config.load_profile("Default")) {
        spdlog::info("No saved profile, using defaults");
        config.save_profile();  // Save default profile for future editing
    }
    auto profile = config.get_profile();
    mapper.apply_profile(profile);
    spdlog::info("Profile '{}' applied", profile.name);

    // Run input loop
    std::thread input_thread(input_loop,
                              std::ref(neptune),
                              std::ref(xbox),
                              std::ref(mapper),
                              std::ref(synth));

    spdlog::info("WinDeck running. Type 'help' for commands, Ctrl+C to stop.");

    // Console command processing (runs on a separate blocking thread)
    auto process_command = [&](const std::string& line) {
        auto profile = config.get_profile();
        bool changed = false;

        if (line == "help") {
            printf("Commands:\n");
            printf("  rpad trackpad|mouse|wasd|arrows|scroll|disabled\n");
            printf("  lpad trackpad|mouse|wasd|arrows|scroll|disabled\n");
            printf("  lstick stick|wasd|arrows|mouse\n");
            printf("  rstick stick|wasd|arrows|mouse\n");
            printf("  dpad dpad|wasd|arrows\n");
            printf("  gyro off|mouse|rstick\n");
            printf("  save   - save current profile\n");
            printf("  status - show current modes\n");
        } else if (line.substr(0, 5) == "rpad " || line.substr(0, 5) == "lpad ") {
            bool is_right = (line[0] == 'r');
            auto& pad = is_right ? profile.right_touchpad : profile.left_touchpad;
            auto mode_str = line.substr(5);
            if (mode_str == "trackpad")     pad.mode = windeck::config::TouchpadMode::TrackpadInertia;
            else if (mode_str == "mouse")   pad.mode = windeck::config::TouchpadMode::Mouse;
            else if (mode_str == "wasd")    pad.mode = windeck::config::TouchpadMode::Wasd;
            else if (mode_str == "arrows")  pad.mode = windeck::config::TouchpadMode::Arrows;
            else if (mode_str == "scroll")  pad.mode = windeck::config::TouchpadMode::Scroll;
            else if (mode_str == "disabled") pad.mode = windeck::config::TouchpadMode::Disabled;
            else { printf("Unknown mode: %s\n", mode_str.c_str()); return; }
            changed = true;
            printf("%cpad -> %s\n", line[0], mode_str.c_str());
        } else if (line.substr(0, 7) == "lstick " || line.substr(0, 7) == "rstick ") {
            bool is_right = (line[0] == 'r');
            auto& stick = is_right ? profile.right_stick : profile.left_stick;
            auto mode_str = line.substr(7);
            if (mode_str == "stick")        stick.mode = windeck::config::StickMode::Stick;
            else if (mode_str == "wasd")    stick.mode = windeck::config::StickMode::Wasd;
            else if (mode_str == "arrows")  stick.mode = windeck::config::StickMode::Arrows;
            else if (mode_str == "mouse")   stick.mode = windeck::config::StickMode::Mouse;
            else { printf("Unknown mode: %s\n", mode_str.c_str()); return; }
            changed = true;
            printf("%cstick -> %s\n", line[0], mode_str.c_str());
        } else if (line.substr(0, 5) == "dpad ") {
            auto mode_str = line.substr(5);
            if (mode_str == "dpad")         profile.dpad.mode = windeck::config::DpadMode::Dpad;
            else if (mode_str == "wasd")    profile.dpad.mode = windeck::config::DpadMode::Wasd;
            else if (mode_str == "arrows")  profile.dpad.mode = windeck::config::DpadMode::Arrows;
            else { printf("Unknown mode: %s\n", mode_str.c_str()); return; }
            changed = true;
            printf("dpad -> %s\n", mode_str.c_str());
        } else if (line.substr(0, 5) == "gyro ") {
            auto mode_str = line.substr(5);
            if (mode_str == "off")           profile.gyro.mode = windeck::config::GyroMode::Off;
            else if (mode_str == "mouse")    profile.gyro.mode = windeck::config::GyroMode::Mouse;
            else if (mode_str == "rstick")   profile.gyro.mode = windeck::config::GyroMode::RightStick;
            else { printf("Unknown mode: %s\n", mode_str.c_str()); return; }
            changed = true;
            printf("gyro -> %s\n", mode_str.c_str());
        } else if (line == "save") {
            config.save_profile();
            printf("Profile saved.\n");
        } else if (line == "status") {
            auto p = config.get_profile();
            const char* pad_modes[] = {"trackpad","mouse","wasd","arrows","radial","dpad","scroll","disabled"};
            const char* stick_modes[] = {"stick","wasd","arrows","mouse"};
            const char* dpad_modes[] = {"dpad","wasd","arrows"};
            const char* gyro_modes[] = {"off","rstick","mouse"};
            auto safe = [](const char* arr[], size_t size, int idx) -> const char* {
                return (idx >= 0 && static_cast<size_t>(idx) < size) ? arr[idx] : "?";
            };
            printf("rpad=%s lpad=%s lstick=%s rstick=%s dpad=%s gyro=%s\n",
                safe(pad_modes, 8, static_cast<int>(p.right_touchpad.mode)),
                safe(pad_modes, 8, static_cast<int>(p.left_touchpad.mode)),
                safe(stick_modes, 4, static_cast<int>(p.left_stick.mode)),
                safe(stick_modes, 4, static_cast<int>(p.right_stick.mode)),
                safe(dpad_modes, 3, static_cast<int>(p.dpad.mode)),
                safe(gyro_modes, 3, static_cast<int>(p.gyro.mode)));
        } else {
            printf("Unknown command. Type 'help'.\n");
        }

        if (changed) {
            config.set_profile(profile);
            mapper.apply_profile(profile);
        }
    };

    // Console reader thread: blocks on std::getline, dispatches commands
    std::thread console_thread([&]() {
        std::string line;
        while (g_running.load(std::memory_order_relaxed)) {
            if (!std::getline(std::cin, line)) break; // EOF or error
            if (!line.empty()) {
                process_command(line);
            }
        }
    });

    // Main thread: process UI events and wait for shutdown
    while (g_running.load(std::memory_order_relaxed)) {
        if (g_show_console.exchange(false, std::memory_order_relaxed)) {
            HWND console = GetConsoleWindow();
            if (console) {
                // GetConsoleWindow() returns the ConPTY pseudo-window when
                // Windows Terminal is the default console. The actual visible
                // window is the root owner in the window hierarchy.
                HWND target = GetAncestor(console, GA_ROOTOWNER);
                if (!target) target = console;
                if (GetForegroundWindow() == target) {
                    ShowWindow(target, SW_MINIMIZE);
                } else {
                    if (IsIconic(target))
                        ShowWindow(target, SW_RESTORE);
                    SetForegroundWindow(target);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    spdlog::info("Shutting down...");
    spdlog::default_logger()->flush();

    console_thread.detach(); // Blocked on getline — let it die with process
    input_thread.join();
    xbox.disconnect();
    g_neptune = nullptr;
    neptune.close();

    spdlog::info("WinDeck stopped");
    spdlog::default_logger()->flush();
    CloseHandle(single_mutex);
    return 0;
}
