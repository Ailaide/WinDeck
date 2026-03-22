#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "neptune/neptune_device.h"
#include "output/virtual_xbox.h"
#include "mapper/mapper.h"
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

// Handle Windows console events (close button, logoff, shutdown)
static BOOL WINAPI console_handler(DWORD event) {
    switch (event) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            g_running.store(false, std::memory_order_relaxed);
            // Give the main thread time to clean up
            Sleep(3000);
            return TRUE;
    }
    return FALSE;
}

// Launch Windows on-screen keyboard (TabTip)
static void launch_on_screen_keyboard() {
    spdlog::info("Launching on-screen keyboard");
    ShellExecuteW(nullptr, L"open", L"osk.exe", nullptr, nullptr, SW_SHOWNORMAL);
}

static void input_loop(windeck::neptune::NeptuneDevice& device,
                        windeck::output::VirtualXbox& xbox,
                        windeck::mapper::Mapper& mapper) {
    // Elevate thread priority for minimal input latency
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif

    windeck::neptune::InputReport input{};
    uint64_t error_count = 0;
    uint64_t report_count = 0;
    constexpr int kMaxConsecutiveErrors = 50; // ~200ms of errors before reconnect

    // Track previous state for change-based logging (no allocations)
    uint32_t last_btn = 0, last_btn_ext = 0;

    auto reset_tracking = [&]() {
        last_btn = 0; last_btn_ext = 0;
    };

    spdlog::info("Input loop started");

    while (g_running.load(std::memory_order_relaxed)) {
        int read_result = device.read(input);

        if (read_result > 0) {
            error_count = 0;
            report_count++;

            // Map and send to virtual Xbox — this is the hot path, zero allocations
            auto result = mapper.map(input);
            xbox.update(result.xbox_report);

            // Handle system buttons (rare events, logging is fine)
            if (result.sys_events.steam_pressed) {
                spdlog::info("Steam button pressed");
            }
            if (result.sys_events.quick_access_pressed) {
                launch_on_screen_keyboard();
            }

            // Log only button changes (no string formatting in hot path when idle)
            bool btn_changed = (input.buttons != last_btn) || (input.buttons_ext != last_btn_ext);
            if (btn_changed) {
                spdlog::debug("btn={:#010x} ext={:#010x}", input.buttons, input.buttons_ext);
                last_btn = input.buttons;
                last_btn_ext = input.buttons_ext;
            }
        } else if (read_result == 0) {
            // Timeout — normal, no new data
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
                    reset_tracking();
                } else {
                    spdlog::error("Reconnect failed, retrying in 3 seconds...");
                    std::this_thread::sleep_for(std::chrono::seconds(3));
                    error_count = 0;
                }
            }
        }
    }

    spdlog::info("Input loop stopped");
}

// Get directory where the exe is located
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
        logger->set_level(spdlog::level::debug);
        logger->flush_on(spdlog::level::debug);

        spdlog::set_default_logger(logger);
    } catch (const std::exception& e) {
        // Fallback to console only
        spdlog::set_level(spdlog::level::debug);
        spdlog::warn("Failed to create log file: {}", e.what());
    }
}

int main() {
    setup_logging();
    spdlog::info("WinDeck v0.1.0 starting");

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    SetConsoleCtrlHandler(console_handler, TRUE);

    // Ensure ViGEmBus driver is installed (auto-download and install if missing)
    if (!windeck::driver::ensure_vigembus_installed()) {
        spdlog::error("Cannot proceed without ViGEmBus driver");
        return 1;
    }

    // Connect virtual Xbox 360 controller
    windeck::output::VirtualXbox xbox;
    if (!xbox.connect()) {
        spdlog::error("Failed to connect to ViGEmBus despite driver being present");
        return 1;
    }

    // Open Neptune controller (after driver setup to avoid handle invalidation)
    windeck::neptune::NeptuneDevice neptune;
    if (!neptune.open()) {
        spdlog::error("Cannot proceed without Neptune controller");
        return 1;
    }

    // Create mapper (MVP: hardcoded 1:1 mapping)
    windeck::mapper::Mapper mapper;

    // Run input loop on a dedicated thread
    std::thread input_thread(input_loop,
                              std::ref(neptune),
                              std::ref(xbox),
                              std::ref(mapper));

    spdlog::info("WinDeck running. Press Ctrl+C to stop.");

    // Main thread: wait for shutdown signal
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
