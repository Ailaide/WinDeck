cmake_minimum_required(VERSION 3.21)
project(WinDeck VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- vcpkg dependencies ---
find_package(hidapi CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# --- ViGEmClient (vendored) ---
add_library(ViGEmClient STATIC
    extern/ViGEmClient/src/ViGEmClient.cpp
)
target_include_directories(ViGEmClient PUBLIC
    extern/ViGEmClient/include
)
target_link_libraries(ViGEmClient PRIVATE setupapi)

# --- WinDeck executable ---
add_executable(windeck
    src/main.cpp
    src/neptune/neptune_device.cpp
    src/output/virtual_xbox.cpp
    src/output/synthetic_input.cpp
    src/mapper/mapper.cpp
    src/mapper/button_mapper.cpp
    src/mapper/stick_mapper.cpp
    src/mapper/trigger_mapper.cpp
    src/driver/driver_manager.cpp
    src/config/config.cpp
    src/hidhide/hidhide_client.cpp
    src/mapper/dpad_mapper.cpp
    src/mapper/touchpad_mapper.cpp
    src/mapper/gyro_mapper.cpp
    src/modes/directional_mode.cpp
    src/modes/mouse_mode.cpp
    src/modes/inertia.cpp
    src/console/console_commands.cpp
)

target_include_directories(windeck PRIVATE src)

# Embed application manifest (UAC level + WinRT compatibility)
if(MSVC)
    target_sources(windeck PRIVATE "${CMAKE_SOURCE_DIR}/resources/windeck.manifest")
    target_link_options(windeck PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${CMAKE_SOURCE_DIR}/resources/windeck.manifest")
endif()

target_link_libraries(windeck PRIVATE
    hidapi::hidapi
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    ViGEmClient
    setupapi
    urlmon
)

# Auto-deploy release binary to Steam Deck network share after successful build.
if(WIN32)
    set(WINDECK_DEPLOY_DIR "//STEAMDECK/Shared")
    add_custom_command(TARGET windeck POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${WINDECK_DEPLOY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:windeck>"
            "${WINDECK_DEPLOY_DIR}/windeck.exe"
        COMMENT "Deploying windeck.exe to ${WINDECK_DEPLOY_DIR}"
    )
endif()
