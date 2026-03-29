#pragma once

#include "overlay/overlay_base.h"
#include "config/config.h"
#include "mapper/mapper.h"
#include "output/synthetic_input.h"
#include <array>
#include <string>
#include <atomic>
#include <mutex>

namespace windeck::overlay {

// Semi-transparent Quick Access Menu overlay.
// Shows a 3x2 tile grid + parameter rows, navigated with D-pad + A button.
// All public methods are thread-safe (called from input loop thread).
class QuickAccessOverlay : public OverlayBase {
public:
    // Tile grid (1.5x scale)
    static constexpr int kCols   = 3;
    static constexpr int kRows   = 2;
    static constexpr int kTiles  = kCols * kRows;
    static constexpr int kTileW  = 258;
    static constexpr int kTileH  = 114;
    static constexpr int kGap    = 12;
    static constexpr int kPad    = 24;

    // Parameters section
    static constexpr int kParams   = 5;
    static constexpr int kParamH   = 42;
    static constexpr int kParamGap = 9;
    static constexpr int kSepGap   = 18;

    // Total items (tiles + params)
    static constexpr int kTotalItems = kTiles + kParams;

    // Window dimensions
    static constexpr int kTotalW = kPad * 2 + kCols * kTileW + (kCols - 1) * kGap;
    static constexpr int kTilesH = kPad * 2 + kRows * kTileH + (kRows - 1) * kGap;
    static constexpr int kTotalH = kTilesH + kSepGap + kParams * (kParamH + kParamGap) - kParamGap + kPad;

    QuickAccessOverlay(windeck::config::Config& config,
                       windeck::mapper::Mapper& mapper,
                       windeck::output::SyntheticInput& synth);

    void show();
    void hide();

    // D-pad navigation. dx/dy mean different things depending on zone:
    // Tiles: dx = columns, dy = rows (with cross-zone transitions)
    // Params: dy = prev/next param, dx = slider adjust
    void navigate(int dx, int dy);

    // A button: cycle tile mode or button-type param.
    void activate();

    bool is_visible() const { return visible_.load(std::memory_order_relaxed); }
    void update_tiles(const config::Profile& profile);

private:
    // Tile IDs (row-major):
    // [0] D-Pad    [1] L-Stick   [2] R-Stick
    // [3] Gyro     [4] L-Pad     [5] R-Pad
    enum TileId { DPad = 0, LStick, RStick, Gyro, LPad, RPad };

    // Parameter IDs (indices 6-10)
    enum ParamId {
        ParamSensitivity = kTiles,     // 6
        ParamFriction,                 // 7
        ParamAcceleration,             // 8
        ParamCurve,                    // 9
        ParamOverlap                   // 10
    };

    enum class ParamType { Slider, Button };

    struct TileData {
        std::wstring label;
        std::wstring mode_text;
    };

    struct ParamData {
        std::wstring label;
        std::wstring value_text;
        ParamType    type;
        float        value;     // normalized 0-1 for sliders
        float        min_val;
        float        max_val;
        float        step;
    };

    void on_paint() override;
    void on_show(WPARAM wp) override;
    HRESULT create_child_resources() override;
    void release_child_resources() override;

    void draw_tile(int index, int cursor);
    void draw_icon(int index, float x, float y);
    void draw_param(int index, int cursor);
    void reposition();
    void apply_and_refresh();
    void refresh_params(const config::Profile& profile);

    std::atomic<bool> visible_{false};
    std::atomic<int>  cursor_{0};

    mutable std::mutex tiles_mtx_;
    std::array<TileData, kTiles> tiles_;
    std::array<ParamData, kParams> params_;

    windeck::config::Config&         config_;
    windeck::mapper::Mapper&         mapper_;
    windeck::output::SyntheticInput& synth_;

    IDWriteTextFormat* label_fmt_ = nullptr;  // small labels
    IDWriteTextFormat* mode_fmt_  = nullptr;  // large mode text
    ID2D1SolidColorBrush* br_slider_bg_ = nullptr;
};

} // namespace windeck::overlay
