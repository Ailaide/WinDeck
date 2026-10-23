#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace windeck::output {

// Shows and hides the Windows touch keyboard.
//
// Requests are queued and processed one at a time on a worker thread: they are
// COM calls into other processes (TabTip.exe, TextInputHost.exe) and must not
// stall the input loop or an overlay's message pump. After a toggle, the worker
// waits (at most 2 s) for the keyboard to finish showing or hiding before it
// starts the next request. A request is skipped, with a warning, when the
// keyboard's state cannot be read. Queued requests still run when the object
// is destroyed.
class TouchKeyboard {
public:
    TouchKeyboard();
    ~TouchKeyboard();

    TouchKeyboard(const TouchKeyboard&) = delete;
    TouchKeyboard& operator=(const TouchKeyboard&) = delete;

    // Show the keyboard if it is hidden, hide it if it is shown.
    void toggle();

    // Show the keyboard unless it is already shown.
    void show();

    // Hide the keyboard if it is shown.
    void hide();

private:
    enum class Request { Toggle, Show, Hide };

    void post(Request request);
    void run();

    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<Request> queue_;   // guarded by mtx_
    bool stop_ = false;           // guarded by mtx_
    std::thread worker_;          // declared last: starts after the members above
};

} // namespace windeck::output
