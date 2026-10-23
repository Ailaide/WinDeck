#pragma once

#include "overlay/overlay_base.h"
#include "config/config.h"
#include "mapper/mapper.h"
#include "output/synthetic_input.h"
#include "output/touch_keyboard.h"
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
    static constexpr int kTileH  = 104;
    static constexpr int kGap    = 12;
    static constexpr int kPad    = 24;

    // Small tiles (bumpers/triggers/back buttons): 4 per row
    static constexpr int kSmallTileCols = 4;
    static constexpr int kSmallTileH = 64;
    // Fill the tile-grid width (kCols tiles + inner gaps) with kSmallTileCols tiles.
    static constexpr int kSmallTileW = (kCols * kTileW + (kCols - 1) * kGap - (kSmallTileCols - 1) * kGap) / kSmallTileCols;

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

    // Profile bar (dropdown + New + Delete), sits above the tiles
    static constexpr int kProfileRowH = 44;
    static constexpr int kProfileGap  = 12;

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

    // Profile bar cursor zone: dropdown, New, Delete
    static constexpr int kZoneProfiles = kTotalItems;                            // 22..24
    static constexpr int kProfileItems = 3;

    // Window dimensions (menu panel only, without action bar/description)
    static constexpr int kContentW = kPad * 2 + kCols * kTileW + (kCols - 1) * kGap;
    static constexpr int kMenuH =
        kPad +                                              // top padding
        kProfileRowH + kProfileGap +                        // profile bar (above tiles)
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
    ~QuickAccessOverlay();

    void show();
    void hide();

    void navigate(int dx, int dy);
    void activate();                     // A button
    void press_x();                      // X button: enter submenu or cycle remap backward
    void press_y();                      // Y button: in the remap picker, clear to None
    void hide_submenu();                 // B in submenu: close submenu
    void cycle_category(int direction);  // LB/RB: switch remap category / picker device tab
    void close_picker();                 // B in picker: return to the grid
    bool is_picker_open() const { return picker_open_.load(std::memory_order_relaxed); }

    bool is_visible() const { return visible_.load(std::memory_order_relaxed); }
    bool is_submenu_open() const { return submenu_open_.load(std::memory_order_relaxed); }
    bool is_dropdown_open() const { return dropdown_open_.load(std::memory_order_relaxed); }
    void close_dropdown();
    bool is_name_entry() const { return name_entry_.load(std::memory_order_relaxed); }
    void cancel_name_entry();
    // Profile name entry shows the touch keyboard and hides it when it ends.
    void set_touch_keyboard(windeck::output::TouchKeyboard* keyboard) { touch_kbd_ = keyboard; }
    void update_tiles(const config::Profile& profile);

private:
    // Tile IDs for main 3x2 (indices kZoneMain..kZoneMain+5)
    enum MainTileId { DPad = 0, LStick, RStick, Gyro, LPad, RPad };



    enum class ParamType { Slider, Button, Toggle, Remap };

    // Where a picker binding is written. Button = a grid button (buttons[bid]);
    // RadialKey/ClickAction = a touchpad action edited from the submenu.
    enum class RemapDest { Button, RadialKey, ClickAction };

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
        RemapDest    remap_dest   = RemapDest::Button;  // for ParamType::Remap
        int          remap_sector = 0;                  // for RadialKey dest
    };

    // A selectable target in the remap picker (a key, mouse button, or Xbox button).
    struct PickerTarget {
        float cx = 0, cy = 0, w = 0, h = 0;   // absolute window coords + size
        std::wstring label;
        config::ButtonAction action;
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
    void draw_icon(int index, float x, float y, ID2D1SolidColorBrush* brush);
    void draw_small_tile(float x, float y, float w, const std::wstring& label,
                         const std::wstring& value, bool selected);
    void draw_mini_tile(float x, float y, float w, const std::wstring& label,
                        const std::wstring& value, bool selected);
    void draw_submenu();

    // Remap picker (Mouse / Keyboard / Xbox tabs)
    void build_picker_targets();
    void open_picker();                  // grid button under the cursor
    void open_picker_submenu(RemapDest dest, int pad_tile, int sector);  // touchpad action from submenu
    void open_picker_for(const config::ButtonAction& current);           // shared: seed tab/cursor, open
    void picker_write(const config::ButtonAction& action);               // write to the active destination
    void picker_navigate(int dx, int dy);
    void picker_switch_tab(int direction);
    void picker_bind_current();
    void picker_set_none();
    void draw_picker();
    void draw_picker_tabs(float x, float y, float w);

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

    // Profile bar
    void navigate_dropdown(int dx, int dy);
    void select_dropdown();
    void activate_profile_item(int item);   // 0=open dropdown, 1=New, 2=Delete
    void refresh_profiles();
    void apply_active_profile();            // apply config's active profile to mapper + refresh UI
    void draw_profile_bar(float y);

    // Profile name entry (New)
    void begin_name_entry();
    void end_name_entry(bool commit);
    void name_append(wchar_t c);
    void name_backspace();
    void draw_name_entry(float y);
    static LRESULT CALLBACK kbd_hook_proc(int code, WPARAM wp, LPARAM lp);

    // Remap helpers
    config::ButtonId button_id_for_cursor(int cursor) const;
    std::wstring action_display(const config::ButtonAction& action) const;

    // Refresh
    void refresh_all(const config::Profile& profile);
    void refresh_all_locked(const config::Profile& profile);

    // State
    std::atomic<bool> visible_{false};
    std::atomic<int>  cursor_{0};
    std::atomic<bool> submenu_open_{false};

    // Remap picker state (all atomics: written on input thread, read while painting)
    std::atomic<bool> picker_open_{false};
    std::atomic<int>  picker_tab_{0};                  // 0=Mouse, 1=Keyboard, 2=Xbox
    std::array<std::atomic<int>, 3> picker_cursor_{};  // focused target per tab
    std::atomic<int>  picker_bid_{-1};                 // ButtonId being remapped (Button dest)
    std::atomic<int>  picker_dest_{0};                 // RemapDest of the active binding
    std::atomic<int>  picker_pad_{0};                  // MainTileId (LPad/RPad) for touchpad dests
    std::atomic<int>  picker_sector_{0};               // radial sector (RadialKey dest)
    std::atomic<bool> picker_from_submenu_{false};     // reopen the submenu when the picker closes
    std::atomic<int>  picker_bound_tab_{-1};           // tab/idx of the current binding
    std::atomic<int>  picker_bound_idx_{-1};
    std::array<std::vector<PickerTarget>, 3> picker_targets_;  // built once in ctor

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

    // Profile bar state (guarded by tiles_mtx_ for the vectors/cursors)
    std::atomic<bool> dropdown_open_{false};
    std::atomic<bool> default_active_{true};  // Default is immutable -> tiles read-only
    int dd_cursor_ = 0;
    int dd_active_ = 0;
    std::vector<std::wstring> dd_names_;

    // Profile name entry (New)
    std::atomic<bool> name_entry_{false};
    std::wstring name_text_;                // guarded by tiles_mtx_
    windeck::output::TouchKeyboard* touch_kbd_ = nullptr;  // Windows touch keyboard
    HHOOK kbd_hook_ = nullptr;              // low-level keyboard hook (overlay thread)

    windeck::config::Config&         config_;
    windeck::mapper::Mapper&         mapper_;
    windeck::output::SyntheticInput& synth_;

    // D2D resources
    IDWriteTextFormat* label_fmt_  = nullptr;
    IDWriteTextFormat* mode_fmt_   = nullptr;
    IDWriteTextFormat* action_fmt_ = nullptr;
    IDWriteTextFormat* desc_fmt_   = nullptr;
    IDWriteTextFormat* mini_fmt_   = nullptr;  // mini tile text
    IDWriteTextFormat* key_fmt_    = nullptr;  // keyboard key labels (picker)
    ID2D1SolidColorBrush* br_slider_bg_ = nullptr;
    ID2D1SolidColorBrush* br_dim_text_  = nullptr;
    ID2D1SolidColorBrush* br_dim_overlay_ = nullptr;  // submenu dim
};

} // namespace windeck::overlay
