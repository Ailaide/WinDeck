cmake_minimum_required(VERSION 3.21)
project(WinDeck VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Sources are UTF-8; compile them as such regardless of the system code page.
if(MSVC)
    add_compile_options(/utf-8)
endif()

# --- vcpkg dependencies ---
find_package(hidapi CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# --- ViGEmClient (vendored) ---
add_library(ViGEmClient STATIC
    extern/ViGEmClient/src/ViGEmClient.cpp
)
# SYSTEM: third-party headers (Client.h is Latin-1 encoded) do not emit warnings.
target_include_directories(ViGEmClient SYSTEM PUBLIC
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
    src/modes/radial_menu_mode.cpp
    src/overlay/overlay_base.cpp
    src/overlay/radial_overlay.cpp
    src/overlay/quick_access_overlay.cpp
)

target_include_directories(windeck PRIVATE src)

# Single source of truth for the version: project(VERSION) above.
target_compile_definitions(windeck PRIVATE WINDECK_VERSION="${PROJECT_VERSION}")

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
    d2d1
    dwrite
)

# --- Optional post-build deploy ---
# Set WINDECK_DEPLOY_DIR (e.g. a network share on the Deck) to copy windeck.exe
# there after every successful build. Empty by default: no deploy.
set(WINDECK_DEPLOY_DIR "" CACHE PATH "Copy windeck.exe to this directory after each build (empty = disabled)")
if(WINDECK_DEPLOY_DIR)
    add_custom_command(TARGET windeck POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${WINDECK_DEPLOY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:windeck>"
            "${WINDECK_DEPLOY_DIR}/windeck.exe"
        COMMENT "Deploying windeck.exe to ${WINDECK_DEPLOY_DIR}"
    )
endif()
