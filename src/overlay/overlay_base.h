#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <atomic>
#include <string>
#include <thread>

namespace windeck::overlay {

// Shared color constants for all overlays.
struct OverlayColors {
    static constexpr D2D1_COLOR_F kSector = {0.149f, 0.149f, 0.149f, 1.0f}; // #262626
    static constexpr D2D1_COLOR_F kHover  = {0.250f, 0.250f, 0.300f, 1.0f}; // blue-gray
    static constexpr D2D1_COLOR_F kActive = {0.902f, 0.600f, 0.098f, 1.0f}; // orange
    static constexpr D2D1_COLOR_F kCenter = {0.078f, 0.078f, 0.078f, 1.0f}; // near-black
    static constexpr D2D1_COLOR_F kText   = {1.000f, 1.000f, 1.000f, 1.0f}; // white
    static constexpr BYTE kGlobalAlpha = 235;
    static constexpr COLORREF kColorKey = RGB(0, 0, 0);
};

// Base class for Direct2D overlay windows.
// Manages a dedicated thread with message loop, D2D/DWrite factories,
// shared brushes, text format, and layered window lifecycle.
//
// Subclasses MUST call start() at the end of their constructor.
// This is necessary because the overlay thread calls virtual methods
// (create_child_resources, on_paint) which require the derived class
// to be fully constructed.
class OverlayBase {
public:
    OverlayBase(const wchar_t* class_name, int width, int height);
    virtual ~OverlayBase();

    OverlayBase(const OverlayBase&) = delete;
    OverlayBase& operator=(const OverlayBase&) = delete;

protected:
    // Call at the end of the derived class constructor to start the overlay thread.
    void start();

    // Override in subclasses
    virtual void on_paint() = 0;
    virtual void on_show(WPARAM wp);
    virtual void on_hide();
    virtual HRESULT create_child_resources() { return S_OK; }
    virtual void release_child_resources() {}

    // Handle custom WM_APP messages in subclasses. Return true if handled.
    virtual bool on_app_message(UINT msg, WPARAM wp, LPARAM lp);

    // Utilities for subclasses
    void post_show(WPARAM wp = 0);
    void post_hide();
    void invalidate();
    HWND hwnd() const { return hwnd_; }

    // D2D resources (accessible to subclasses, overlay thread only)
    ID2D1Factory*           factory_   = nullptr;
    ID2D1HwndRenderTarget*  rt_        = nullptr;
    ID2D1SolidColorBrush*   br_sector_ = nullptr;
    ID2D1SolidColorBrush*   br_hover_  = nullptr;
    ID2D1SolidColorBrush*   br_active_ = nullptr;
    ID2D1SolidColorBrush*   br_center_ = nullptr;
    ID2D1SolidColorBrush*   br_text_   = nullptr;
    IDWriteFactory*         dwrite_    = nullptr;
    IDWriteTextFormat*      text_fmt_  = nullptr;

    int width()  const { return width_; }
    int height() const { return height_; }
    void set_size(int w, int h) { width_ = w; height_ = h; }

    static constexpr UINT WM_OVR_SHOW   = WM_APP + 1;
    static constexpr UINT WM_OVR_HIDE   = WM_APP + 2;
    static constexpr UINT WM_OVR_RESIZE = WM_APP + 3;

    // Attempt to recreate D2D resources after device loss.
    HRESULT recreate_resources();

private:
    void thread_main();
    HRESULT create_resources();
    void release_resources();

    static LRESULT CALLBACK s_wnd_proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT wnd_proc(HWND, UINT, WPARAM, LPARAM);

    HWND hwnd_ = nullptr;
    std::thread thread_;
    std::atomic<bool> initialized_{false};

    const wchar_t* class_name_;
    int width_;
    int height_;
};

} // namespace windeck::overlay
