#pragma once

#include "overlay/overlay_base.h"
#include "config/config.h"
#include "mapper/mapper.h"
#include "output/synthetic_input.h"
#include <array>
#include <string>
#include <atomic>
#include <mutex>
#include <vector>

namespace windeck::overlay {

// Quick Access Menu overlay.
// Tile grid with bumpers/triggers, main 3x2, button remap rows, back buttons.
// Action bar and description drawn outside the menu panel.
// X on main tiles opens a submenu panel to the right.
class QuickAccessOverlay : public OverlayBase {
public:
    // Main tiles grid
    static constexpr int kCols   = 3;
    static constexpr int kRows   = 2;
    static constexpr int kTiles  = kCols * kRows;   // 6
    static constexpr int kTileW  = 258;
    static constexpr int kTileH  = 114;
    static constexpr int kGap    = 12;
    static constexpr int kPad    = 24;

    // Small tiles (bumpers/triggers/back buttons): 4 per row
    static constexpr int kSmallTileCols = 4;
    static constexpr int kSmallTileH = 72;
    static constexpr int kSmallTileW = (kPad * 2 + kCols * kTileW + (kCols - 1) * kGap - 2 * kPad - (kSmallTileCols - 1) * kGap) / kSmallTileCols;
    // Simpler: use same total width minus padding and gaps, divided by 4

    // Mini tiles (button remap): 4 per row
    static constexpr int kMiniTileCols = 4;
    static constexpr int kMiniTileH = 52;
    static constexpr int kMiniGap = 8;

    // Action bar (outside menu panel)
    static constexpr int kActionBarH   = 36;
    static constexpr int kActionBarGap = 12;

    // Description area (outside menu panel)
    static constexpr int kDescAreaH = 52;
    static constexpr int kDescGap   = 12;

    // Submenu panel
    static constexpr int kSubmenuW = 420;

    // Item zones (cursor indices)
    static constexpr int kTopTiles    = 2;   // LB, RB
    static constexpr int kMiniRow1    = 4;   // A, B, X, Y
    static constexpr int kMiniRow2    = 4;   // Start, Back, LSC, RSC
    static constexpr int kBottomTiles = 4;   // L4, L5, R4, R5

    // Zone offsets
    static constexpr int kZoneTop     = 0;                                       // 0..3
    static constexpr int kZoneMain    = kTopTiles;                               // 4..9
    static constexpr int kZoneMini1   = kZoneMain + kTiles;                      // 10..13
    static constexpr int kZoneMini2   = kZoneMini1 + kMiniRow1;                  // 14..17
    static constexpr int kZoneBottom  = kZoneMini2 + kMiniRow2;                  // 18..21
    static constexpr int kTotalItems  = kZoneBottom + kBottomTiles;              // 22

    // Window dimensions (menu panel only, without action bar/description)
    static constexpr int kContentW = kPad * 2 + kCols * kTileW + (kCols - 1) * kGap;
    static constexpr int kMenuH =
        kPad +                                              // top padding
        kSmallTileH + kGap +                                // top row (bumpers/triggers)
        kRows * kTileH + (kRows - 1) * kGap + kGap +       // main 3x2
        kMiniTileH + kMiniGap +                             // mini row 1
        kMiniTileH + kGap +                                 // mini row 2
        kSmallTileH +                                       // bottom row
        kPad;                                               // bottom padding
    static constexpr int kTotalW = kContentW;
    static constexpr int kTotalH = kActionBarH + kActionBarGap + kMenuH + kDescGap + kDescAreaH;

    QuickAccessOverlay(windeck::config::Config& config,
                       windeck::mapper::Mapper& mapper,
                       windeck::output::SyntheticInput& synth);

    void show();
    void hide();

    void navigate(int dx, int dy);
    void activate();                     // A button
    void press_x();                      // X button: enter submenu or cycle remap backward
    void hide_submenu();                 // B in submenu: close submenu
    void cycle_category(int direction);  // LB/RB: switch remap category

    bool is_visible() const { return visible_.load(std::memory_order_relaxed); }
    bool is_submenu_open() const { return submenu_open_.load(std::memory_order_relaxed); }
    void update_tiles(const config::Profile& profile);

private:
    // Tile IDs for main 3x2 (indices kZoneMain..kZoneMain+5)
    enum MainTileId { DPad = 0, LStick, RStick, Gyro, LPad, RPad };



    enum class ParamType { Slider, Button, Toggle, Remap };

    struct TileData {
        std::wstring label;
        std::wstring mode_text;
    };

    struct ParamData {
        std::wstring label;
        std::wstring value_text;
        ParamType    type;
        float        value;
        float        min_val;
        float        max_val;
        float        step;
    };

    // Paint
    void on_paint() override;
    void on_show(WPARAM wp) override;
    bool on_app_message(UINT msg, WPARAM wp, LPARAM lp) override;
    HRESULT create_child_resources() override;
    void release_child_resources() override;

    // Drawing
    void draw_action_bar();
    void draw_button_glyph(float cx, float cy, wchar_t letter);
    void draw_description();
    void draw_icon(int index, float x, float y);
    void draw_small_tile(float x, float y, float w, const std::wstring& label,
                         const std::wstring& value, bool selected);
    void draw_mini_tile(float x, float y, float w, const std::wstring& label,
                        const std::wstring& value, bool selected);
    void draw_submenu();

    // Layout helpers
    float menu_top() const { return static_cast<float>(kActionBarH + kActionBarGap); }
    void reposition();

    // Navigation
    void navigate_main(int dx, int dy);
    void navigate_submenu(int dx, int dy);
    void activate_submenu();

    // Submenu
    void enter_submenu();
    void exit_submenu();
    void build_sub_params(int main_tile, const config::Profile& profile);
    void apply_sub_param(int param_idx);
    void cycle_sub_button(int param_idx);

    // Remap helpers
    void cycle_remap_at_cursor(int direction);
    config::ButtonId button_id_for_cursor(int cursor) const;
    std::wstring action_display(const config::ButtonAction& action) const;

    // Refresh
    void refresh_all(const config::Profile& profile);
    void refresh_all_locked(const config::Profile& profile);

    // State
    std::atomic<bool> visible_{false};
    std::atomic<int>  cursor_{0};
    std::atomic<bool> submenu_open_{false};

    mutable std::mutex tiles_mtx_;
    std::array<TileData, kTiles> tiles_;

    // Remap display cache (for all remap tiles: top + mini1 + mini2 + bottom = 16)
    static constexpr int kRemapTiles = kTopTiles + kMiniRow1 + kMiniRow2 + kBottomTiles; // 16
    std::array<std::wstring, kRemapTiles> remap_display_;  // cached display strings

    // Submenu state
    int submenu_tile_ = 0;
    int sub_cursor_ = 0;
    std::vector<ParamData> sub_params_;
    std::vector<std::wstring> sub_descs_;

    windeck::config::Config&         config_;
    windeck::mapper::Mapper&         mapper_;
    windeck::output::SyntheticInput& synth_;

    // D2D resources
    IDWriteTextFormat* label_fmt_  = nullptr;
    IDWriteTextFormat* mode_fmt_   = nullptr;
    IDWriteTextFormat* action_fmt_ = nullptr;
    IDWriteTextFormat* desc_fmt_   = nullptr;
    IDWriteTextFormat* mini_fmt_   = nullptr;  // mini tile text
    ID2D1SolidColorBrush* br_slider_bg_ = nullptr;
    ID2D1SolidColorBrush* br_dim_text_  = nullptr;
    ID2D1SolidColorBrush* br_dim_overlay_ = nullptr;  // submenu dim
};

} // namespace windeck::overlay
