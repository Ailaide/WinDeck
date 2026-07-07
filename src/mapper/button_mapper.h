#pragma once

#include "neptune/neptune_report.h"
#include "config/profile.h"
#include "output/synthetic_input.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ViGEm/Client.h>
#include <cstdint>
#include <array>

namespace windeck::mapper {

// Flags for system button events (intercepted before remapping).
struct SystemButtonEvents {
    bool steam_pressed        = false;
    bool quick_access_pressed = false;
};

// Maps Neptune buttons to Xbox/keyboard/mouse based on profile configuration.
class ButtonMapper {
public:
    // Apply button mapping from profile.
    void set_config(const std::array<config::ButtonAction, static_cast<size_t>(config::ButtonId::Count)>& actions);

    // Map Neptune buttons. Returns Xbox wButtons, emits keyboard/mouse via synth.
    USHORT map(uint32_t neptune_buttons, uint32_t neptune_buttons_ext,
               SystemButtonEvents& sys_events,
               windeck::output::SyntheticInput& synth);

private:
    // Current button actions
    std::array<config::ButtonAction, static_cast<size_t>(config::ButtonId::Count)> actions_;
    bool configured_ = false;

    // Edge detection (first_frame_ suppresses false rising edges on startup)
    bool first_frame_ = true;
    uint32_t prev_buttons_ = 0;
    uint32_t prev_buttons_ext_ = 0;

    // Helper: check if a Neptune button is pressed
    struct NeptuneButton {
        uint32_t btn_mask;
        uint32_t ext_mask;
    };
    static constexpr NeptuneButton kNeptuneMap[] = {
        {windeck::neptune::btn::kA, 0},              // A
        {windeck::neptune::btn::kB, 0},              // B
        {windeck::neptune::btn::kX, 0},              // X
        {windeck::neptune::btn::kY, 0},              // Y
        {windeck::neptune::btn::kLShoulder, 0},      // LB
        {windeck::neptune::btn::kRShoulder, 0},      // RB
        {windeck::neptune::btn::kOptions, 0},         // Start
        {windeck::neptune::btn::kMenu, 0},            // Back
        {windeck::neptune::btn::kLStickClick, 0},     // LSClick
        {windeck::neptune::btn::kRStickClick, 0},     // RSClick
        {0, windeck::neptune::btn_ext::kL4},          // L4
        {windeck::neptune::btn::kL5, 0},              // L5
        {0, windeck::neptune::btn_ext::kR4},          // R4
        {windeck::neptune::btn::kR5, 0},              // R5
        {windeck::neptune::btn::kLPadClick, 0},       // LPadClick
        {windeck::neptune::btn::kRPadClick, 0},       // RPadClick
    };
};

} // namespace windeck::mapper
