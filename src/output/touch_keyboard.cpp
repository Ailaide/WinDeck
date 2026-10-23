#include "output/touch_keyboard.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>

#include <spdlog/spdlog.h>

#include <chrono>
#include <optional>

namespace windeck::output {

namespace {

// Interface of the touch keyboard host, TabTip.exe. It is not part of the
// Windows SDK. Toggle() shows the keyboard when it is hidden and hides it when
// it is shown, without going through the taskbar button.
MIDL_INTERFACE("37c994e7-432b-4834-a2f7-dce1f13b834b")
ITipInvocation : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Toggle(HWND wnd) = 0;
};

// Class that TabTip.exe registers while it runs. COM never starts TabTip for it.
class DECLSPEC_UUID("4ce576fa-83dc-4f88-951c-9d0782b4e376") UIHostNoLaunch;

// Showing the keyboard takes about 350 ms on a Steam Deck and hiding it about
// 40 ms. The timeout only bounds a toggle that never takes effect.
constexpr auto kTransitionTimeout = std::chrono::seconds(2);
constexpr auto kTransitionPoll    = std::chrono::milliseconds(10);

bool toggle_keyboard() {
    ITipInvocation* tip = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(UIHostNoLaunch), nullptr,
        CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, __uuidof(ITipInvocation),
        reinterpret_cast<void**>(&tip));
    if (FAILED(hr) || !tip) {
        spdlog::warn("Touch keyboard host (TabTip.exe) is not available: {:#x}",
                     static_cast<unsigned>(hr));
        return false;
    }
    hr = tip->Toggle(GetDesktopWindow());
    tip->Release();
    if (FAILED(hr)) {
        spdlog::warn("Touch keyboard toggle failed: {:#x}", static_cast<unsigned>(hr));
        return false;
    }
    return true;
}

// Whether the keyboard is shown: the input pane reports the screen area the
// keyboard covers, and an empty rectangle while it is hidden. Empty when the
// query fails (hr says why), so callers never act on a guess.
std::optional<bool> keyboard_visible(HRESULT& hr) {
    IFrameworkInputPane* pane = nullptr;
    hr = CoCreateInstance(__uuidof(FrameworkInputPane), nullptr,
        CLSCTX_INPROC_SERVER, __uuidof(IFrameworkInputPane),
        reinterpret_cast<void**>(&pane));
    if (FAILED(hr) || !pane) return std::nullopt;
    RECT area{};
    hr = pane->Location(&area);
    pane->Release();
    if (FAILED(hr)) return std::nullopt;
    return area.right > area.left && area.bottom > area.top;
}

// Toggle, then wait until the input pane reports the target state. The toggle
// returns before the keyboard has moved, so without the wait the next request
// would see the state from before this one. A failed query does not end the
// wait: the keyboard may still be moving.
void toggle_to(bool target) {
    if (!toggle_keyboard()) return;
    const auto deadline = std::chrono::steady_clock::now() + kTransitionTimeout;
    while (std::chrono::steady_clock::now() < deadline) {
        HRESULT hr = S_OK;
        std::optional<bool> visible = keyboard_visible(hr);
        if (visible && *visible == target) return;
        std::this_thread::sleep_for(kTransitionPoll);
    }
    spdlog::warn("Touch keyboard did not {} within {} ms", target ? "show" : "hide",
        std::chrono::duration_cast<std::chrono::milliseconds>(kTransitionTimeout).count());
}

} // namespace

TouchKeyboard::TouchKeyboard()
    : worker_([this] { run(); }) {}

TouchKeyboard::~TouchKeyboard() {
    {
        std::lock_guard lock(mtx_);
        stop_ = true;
    }
    cv_.notify_one();
    worker_.join();
}

void TouchKeyboard::toggle() { post(Request::Toggle); }
void TouchKeyboard::show()   { post(Request::Show); }
void TouchKeyboard::hide()   { post(Request::Hide); }

void TouchKeyboard::post(Request request) {
    {
        std::lock_guard lock(mtx_);
        queue_.push_back(request);
    }
    cv_.notify_one();
}

void TouchKeyboard::run() {
    // Multithreaded apartment: on an STA thread without a message loop, the
    // input pane reported an empty rectangle even while the keyboard was shown.
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(co)) {
        spdlog::warn("Touch keyboard thread: CoInitializeEx failed: {:#x}",
                     static_cast<unsigned>(co));
    }

    for (;;) {
        Request request;
        {
            std::unique_lock lock(mtx_);
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            // Finish queued requests before stopping, so a hide() posted
            // during shutdown still runs.
            if (queue_.empty()) break;
            request = queue_.front();
            queue_.pop_front();
        }

        // Every request needs the current state: a toggle sent without it could
        // not be waited for, and the next request would act on a stale state.
        HRESULT hr = S_OK;
        std::optional<bool> visible = keyboard_visible(hr);
        if (!visible) {
            spdlog::warn("Cannot tell whether the touch keyboard is shown ({:#x}); request skipped",
                         static_cast<unsigned>(hr));
            continue;
        }
        switch (request) {
            case Request::Toggle: toggle_to(!*visible); break;
            case Request::Show:   if (!*visible) toggle_to(true); break;
            case Request::Hide:   if (*visible) toggle_to(false); break;
        }
    }

    if (SUCCEEDED(co)) CoUninitialize();
}

} // namespace windeck::output
