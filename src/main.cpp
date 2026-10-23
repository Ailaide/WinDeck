#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "neptune/neptune_device.h"
#include "output/virtual_xbox.h"
#include "output/synthetic_input.h"
#include "output/touch_keyboard.h"
#include "mapper/mapper.h"
#include "config/config.h"
#include "hidhide/hidhide_client.h"
#include "driver/driver_manager.h"
#include "overlay/radial_overlay.h"
#include "overlay/quick_access_overlay.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <atomic>
#include <csignal>
#include <thread>
#include <chrono>
#include <filesystem>

static std::atomic<bool> g_running{true};
static windeck::neptune::NeptuneDevice* g_neptune = nullptr;

static void signal_handler(int /*sig*/) {
    g_running.store(false, std::memory_order_relaxed);
}

static BOOL WINAPI console_handler(DWORD event) {
    switch (event) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
            // Request shutdown. The input thread owns the HID device and always
            // restores lizard mode when it exits (see input_loop), so no
            // separate restore flag is needed on any termination path.
            g_running.store(false, std::memory_order_relaxed);
            Sleep(3000);
            return TRUE;
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            // The OS terminates the process shortly after this handler returns,
            // so hold here long enough for the input thread to exit its loop and
            // restore lizard mode before we return.
            g_running.store(false, std::memory_order_relaxed);
            Sleep(5000);
            return TRUE;
    }
    return FALSE;
}

static void input_loop(windeck::neptune::NeptuneDevice& device,
                        windeck::output::VirtualXbox& xbox,
                        windeck::mapper::Mapper& mapper,
                        windeck::output::SyntheticInput& synth,
                        windeck::overlay::QuickAccessOverlay& qam,
                        windeck::output::TouchKeyboard& touch_keyboard,
                        windeck::config::Config& config) {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif

    using namespace windeck::neptune;

    InputReport input{};
    uint64_t error_count = 0;
    constexpr int kMaxConsecutiveErrors = 50;

    // Local edge detection for QAM navigation (separate from ButtonMapper)
    uint32_t prev_nav_btn = 0;
    bool prev_qam_open = false;

    // Owned (not detached) background saver for profile auto-save on QAM close.
    // Joined before this function returns — which happens before main() destroys
    // `config` — so the saver can never outlive the object it writes.
    std::thread save_thread;

    spdlog::info("Input loop started");

    while (g_running.load(std::memory_order_relaxed)) {
        int read_result = device.read(input);

        if (read_result > 0) {
            error_count = 0;

            bool qam_open = qam.is_visible();

            // Auto-save profile when QAM closes (off input thread to avoid I/O stall).
            // Join any prior save first so saves never overlap on the same temp file.
            if (prev_qam_open && !qam_open) {
                if (save_thread.joinable()) save_thread.join();
                save_thread = std::thread([&config]() { config.save_profile(); });
            }
            prev_qam_open = qam_open;

            if (qam_open) {
                // Quick Access Menu is open — intercept D-pad/A/B/Steam for navigation,
                // block all other input from reaching the game.
                uint32_t rising = input.buttons & ~prev_nav_btn;
                prev_nav_btn = input.buttons;

                if (rising & btn::kDpadUp)    qam.navigate(0, -1);
                if (rising & btn::kDpadDown)  qam.navigate(0, 1);
                if (rising & btn::kDpadLeft)  qam.navigate(-1, 0);
                if (rising & btn::kDpadRight) qam.navigate(1, 0);
                if (rising & btn::kA)         qam.activate();
                if (rising & btn::kX)         qam.press_x();
                if (rising & btn::kY)         qam.press_y();
                if (rising & btn::kLShoulder) qam.cycle_category(-1);
                if (rising & btn::kRShoulder) qam.cycle_category(1);
                if (rising & btn::kB) {
                    if (qam.is_name_entry()) qam.cancel_name_entry();
                    else if (qam.is_picker_open()) qam.close_picker();
                    else if (qam.is_submenu_open()) qam.hide_submenu();
                    else if (qam.is_dropdown_open()) qam.close_dropdown();
                    else qam.hide();
                }
                if (rising & btn::kSteam)     qam.hide();

                // Send zeroed report to Xbox — game sees no input
                XUSB_REPORT zero{};
                xbox.update(zero);

            } else {
                // Normal input path (map+tick+flush under one lock)
                auto result = mapper.map_and_flush(input, synth);
                xbox.update(result.xbox_report);

                // Rumble passthrough (time-throttled)
                static auto next_rumble_send = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();
                if (now >= next_rumble_send) {
                    next_rumble_send = now + std::chrono::milliseconds(32);
                    auto rumble = xbox.get_rumble();
                    uint16_t left  = static_cast<uint16_t>(rumble.right_motor) * 257;
                    uint16_t right = static_cast<uint16_t>(rumble.left_motor) * 257;
                    device.send_rumble(left, right);
                }

                // Steam button -> open Quick Access Menu
                if (result.sys_events.steam_pressed) {
                    mapper.release_all(synth);
                    synth.release_all();
                    synth.flush();
                    XUSB_REPORT zero{};
                    xbox.update(zero);

                    qam.update_tiles(config.get_profile());
                    qam.show();
                    prev_nav_btn = input.buttons; // sync edge detection
                }

                // Quick Access button -> show or hide the touch keyboard
                if (result.sys_events.quick_access_pressed) {
                    spdlog::info("Quick Access button pressed, toggling touch keyboard");
                    touch_keyboard.toggle();
                }
            }
        } else if (read_result == 0) {
            // Timeout — tick inertia only when QAM is closed
            if (!qam.is_visible()) {
                mapper.tick_and_flush(synth);
            }
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

    // Finish any in-flight profile save before returning so the background saver
    // never outlives `config` (owned by main, destroyed only after this thread joins).
    if (save_thread.joinable()) save_thread.join();

    mapper.release_all(synth);
    synth.release_all();

    // The input thread owns the HID device, so it always restores lizard mode when
    // it stops — on every shutdown path (Ctrl+C, window close, logoff, shutdown,
    // normal quit). Doing it here guarantees the restore runs before the process is
    // torn down and keeps all HID I/O on the owning thread. main() also calls
    // neptune.close() afterwards, whose restore is idempotent (safety net).
    device.enable_lizard_mode();
    spdlog::info("Lizard mode restored on input-thread exit");
    spdlog::default_logger()->flush();

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
    spdlog::info("WinDeck v{} starting", WINDECK_VERSION);

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

    windeck::overlay::RadialOverlay overlay;
    mapper.set_overlay(&overlay);
    mapper.set_device(&neptune);

    // Load the last-active profile. Default is synthetic (built-in values).
    std::string startup_profile = config.persisted_active();
    if (!config.load_profile(startup_profile)) {
        config.load_profile(windeck::config::Config::kDefaultProfileName);
    }
    auto profile = config.get_profile();
    mapper.apply_profile(profile, synth);
    spdlog::info("Profile '{}' applied", profile.name);

    // Declared before qam so it outlives the overlay that posts requests to it.
    windeck::output::TouchKeyboard touch_keyboard;

    windeck::overlay::QuickAccessOverlay qam(config, mapper, synth);
    qam.set_touch_keyboard(&touch_keyboard);

    // Run input loop
    std::thread input_thread(input_loop,
                              std::ref(neptune),
                              std::ref(xbox),
                              std::ref(mapper),
                              std::ref(synth),
                              std::ref(qam),
                              std::ref(touch_keyboard),
                              std::ref(config));

    spdlog::info("WinDeck running. Press Ctrl+C to stop.");

    // Main thread: wait for shutdown
    while (g_running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    spdlog::info("Shutting down...");
    spdlog::default_logger()->flush();

    input_thread.join();
    // Close the touch keyboard if a profile name was being typed. The input
    // thread has stopped, so nothing else changes the name entry now.
    qam.cancel_name_entry();
    xbox.disconnect();
    g_neptune = nullptr;
    neptune.close();

    spdlog::info("WinDeck stopped");
    spdlog::default_logger()->flush();
    CloseHandle(single_mutex);
    return 0;
}
