#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

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
#include <thread>
#include <chrono>
#include <filesystem>

static std::atomic<bool> g_running{true};

static void signal_handler(int /*sig*/) {
    g_running.store(false, std::memory_order_relaxed);
}

static BOOL WINAPI console_handler(DWORD event) {
    switch (event) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            g_running.store(false, std::memory_order_relaxed);
            Sleep(3000);
            return TRUE;
    }
    return FALSE;
}

static void toggle_on_screen_keyboard() {
    // Find existing osk.exe window
    HWND hwnd = FindWindowW(L"OSKMainClass", nullptr);
    if (hwnd) {
        // Already running — close it
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    } else {
        // Not running — launch it
        ShellExecuteW(nullptr, L"open", L"osk.exe", nullptr, nullptr, SW_SHOWNORMAL);
    }
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
            xbox.update(result.xbox_report);
            synth.flush();

            // Rumble passthrough (throttled)
            static uint8_t rumble_tick = 0;
            if (++rumble_tick >= 8) {
                rumble_tick = 0;
                auto rumble = xbox.get_rumble();
                uint16_t left  = static_cast<uint16_t>(rumble.right_motor) * 257;
                uint16_t right = static_cast<uint16_t>(rumble.left_motor) * 257;
                device.send_rumble(left, right);
            }

            // System buttons
            if (result.sys_events.steam_pressed) {
                HWND console = GetConsoleWindow();
                if (console) {
                    ShowWindow(console, SW_RESTORE);
                    SetForegroundWindow(console);
                }
            }
            if (result.sys_events.quick_access_pressed) {
                toggle_on_screen_keyboard();
            }
        } else if (read_result == 0) {
            // Timeout
        } else {
            error_count++;
            if (error_count == 1) {
                spdlog::warn("HID read issue detected, monitoring...");
            }
            if (error_count >= kMaxConsecutiveErrors) {
                spdlog::error("Too many consecutive HID errors, attempting reconnect...");
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

    synth.release_all();
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

    // Hide Neptune from other apps via HidHide (non-fatal if not installed)
    windeck::hidhide::HidHideClient hidhide;
    hidhide.hide_neptune();

    // Load config and apply profile
    windeck::config::Config config;
    windeck::mapper::Mapper mapper;
    windeck::output::SyntheticInput synth;

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

    spdlog::info("WinDeck running. Press Ctrl+C to stop.");

    // Main thread: wait for shutdown
    while (g_running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    spdlog::info("Shutting down...");

    input_thread.join();
    xbox.disconnect();
    neptune.close();

    spdlog::info("WinDeck stopped");
    return 0;
}
