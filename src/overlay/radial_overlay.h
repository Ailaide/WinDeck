#pragma once

#include "overlay/overlay_base.h"
#include <array>
#include <string>
#include <atomic>
#include <mutex>

namespace windeck::overlay {

// Translucent Direct2D radial menu overlay window.
// Inherits D2D boilerplate from OverlayBase. All public methods are thread-safe.
class RadialOverlay : public OverlayBase {
public:
    static constexpr int kWidth  = 220;
    static constexpr int kHeight = 220;

    RadialOverlay();

    // Set sector labels — call at profile load (thread-safe).
    void set_labels(const std::array<std::string, 8>& labels);

    // Show overlay for given pad side (0=right, 1=left).
    void show(uint8_t side);

    // Update hovered sector + pressed state. Only acts if side matches current.
    void update_sector(int hovered, bool pressed, uint8_t side);

    // Hide overlay. Only acts if side matches current.
    void hide(uint8_t side);

protected:
    void on_paint() override;
    void on_show(WPARAM wp) override;
    HRESULT create_child_resources() override;
    void release_child_resources() override;

private:
    void draw_wedge(int sector, int hovered, bool pressed);
    void draw_label(int sector);
    void build_wedge_geometry();
    void reposition(uint8_t side);

    // Cached wedge geometry (rebuilt on create_resources)
    ID2D1PathGeometry* wedge_geom_[8] = {};

    // Shared state (input thread -> overlay thread)
    std::atomic<int>     active_sector_{-1};
    std::atomic<bool>    pressed_{false};
    std::atomic<uint8_t> side_{0};

    mutable std::mutex          labels_mtx_;
    std::array<std::wstring, 8> labels_{L"1",L"2",L"3",L"4",L"5",L"6",L"7",L"8"};
};

} // namespace windeck::overlay
