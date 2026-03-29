#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <array>
#include <string>
#include <atomic>
#include <mutex>
#include <thread>

namespace windeck::overlay {

// Translucent Direct2D radial menu overlay window.
// Runs on a dedicated thread. All public methods are thread-safe.
class RadialOverlay {
public:
    static constexpr int kWidth  = 220;
    static constexpr int kHeight = 220;

    RadialOverlay();
    ~RadialOverlay();

    RadialOverlay(const RadialOverlay&) = delete;
    RadialOverlay& operator=(const RadialOverlay&) = delete;

    // Set sector labels — call at profile load (thread-safe).
    void set_labels(const std::array<std::string, 8>& labels);

    // Show overlay for given pad side (0=right, 1=left).
    void show(uint8_t side);

    // Update hovered sector + pressed state. Only acts if side matches current.
    void update_sector(int hovered, bool pressed, uint8_t side);

    // Hide overlay. Only acts if side matches current.
    void hide(uint8_t side);

private:
    void thread_main();
    void on_show(uint8_t side);
    void on_hide();
    void on_paint();
    void draw_wedge(int sector, int hovered, bool pressed);
    void draw_label(int sector);
    HRESULT create_resources();
    void    release_resources();
    void    reposition(uint8_t side);

    static LRESULT CALLBACK s_wnd_proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT wnd_proc(HWND, UINT, WPARAM, LPARAM);

    HWND            hwnd_        = nullptr;
    std::thread     thread_;
    std::atomic<bool> initialized_{false};

    // D2D/DWrite resources (overlay thread only)
    ID2D1Factory*           factory_   = nullptr;
    ID2D1HwndRenderTarget*  rt_        = nullptr;
    ID2D1SolidColorBrush*   br_sector_ = nullptr;
    ID2D1SolidColorBrush*   br_hover_  = nullptr;
    ID2D1SolidColorBrush*   br_active_ = nullptr;
    ID2D1SolidColorBrush*   br_center_ = nullptr;
    ID2D1SolidColorBrush*   br_text_   = nullptr;
    IDWriteFactory*         dwrite_    = nullptr;
    IDWriteTextFormat*      text_fmt_  = nullptr;

    // Shared state (input thread -> overlay thread)
    std::atomic<int>     active_sector_{-1};
    std::atomic<bool>    pressed_{false};
    std::atomic<uint8_t> side_{0};

    mutable std::mutex          labels_mtx_;
    std::array<std::wstring, 8> labels_{L"1",L"2",L"3",L"4",L"5",L"6",L"7",L"8"};

    static constexpr UINT     WM_OVR_SHOW  = WM_APP + 1;
    static constexpr UINT     WM_OVR_HIDE  = WM_APP + 2;
    static constexpr COLORREF kColorKey    = RGB(0, 0, 0);
    static constexpr BYTE     kGlobalAlpha = 220;
};

} // namespace windeck::overlay
