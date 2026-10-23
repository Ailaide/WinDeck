#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ViGEm/Client.h>
#include <array>
#include <cstdint>

namespace windeck::output {

// Producers of Xbox button bits.
enum class XboxSource : uint8_t { Buttons, Dpad, LeftTrigger, RightTrigger, Count };

// The virtual Xbox report, kept as each source's last contribution. A source
// that is not updated keeps its last value, so when only the triggers run (a
// tap ending between reports), the sticks, D-pad and buttons keep theirs, and a
// bit held by two sources stays set until both drop it.
class XboxState {
public:
    void set_buttons(XboxSource source, USHORT bits) {
        buttons_[static_cast<size_t>(source)] = bits;
    }
    void set_left_stick(SHORT x, SHORT y)  { lx_ = x; ly_ = y; }
    void set_right_stick(SHORT x, SHORT y) { rx_ = x; ry_ = y; }
    void set_left_trigger(BYTE v)  { lt_ = v; }
    void set_right_trigger(BYTE v) { rt_ = v; }

    // The combined report: button bits OR-ed across sources.
    XUSB_REPORT report() const {
        XUSB_REPORT r{};
        for (USHORT b : buttons_) r.wButtons |= b;
        r.bLeftTrigger  = lt_;
        r.bRightTrigger = rt_;
        r.sThumbLX = lx_; r.sThumbLY = ly_;
        r.sThumbRX = rx_; r.sThumbRY = ry_;
        return r;
    }

    // Neutral report, every contribution forgotten (menu open, profile change).
    void clear() { *this = XboxState{}; }

private:
    std::array<USHORT, static_cast<size_t>(XboxSource::Count)> buttons_{};
    SHORT lx_ = 0, ly_ = 0, rx_ = 0, ry_ = 0;
    BYTE  lt_ = 0, rt_ = 0;
};

} // namespace windeck::output
