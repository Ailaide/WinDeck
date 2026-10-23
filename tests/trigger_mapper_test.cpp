#include "mapper/trigger_mapper.h"
#include "output/xbox_state.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <vector>

using windeck::config::ActionType;
using windeck::config::BindingMode;
using windeck::config::ClickStrength;
using windeck::config::TriggerBinding;
using windeck::config::TriggerConfig;
using windeck::config::TriggerMode;
using windeck::mapper::TriggerFrame;
using windeck::mapper::TriggerMapper;
using windeck::output::HoldSource;
using windeck::output::SyntheticInput;

// Every SyntheticInput here sends into a capture, never to the system, and
// time is a fixed clock the test advances by hand.

namespace {

using Clock = TriggerMapper::Clock;
using namespace std::chrono_literals;

Clock::time_point at(int ms) { return Clock::time_point{} + std::chrono::milliseconds(ms); }

struct Capture {
    int key_downs = 0, key_ups = 0, wheel_events = 0;
    SyntheticInput::Sender sender() {
        return [this](const INPUT* in, UINT n) {
            for (UINT i = 0; i < n; i++) {
                if (in[i].type == INPUT_KEYBOARD) {
                    if (in[i].ki.dwFlags & KEYEVENTF_KEYUP) key_ups++; else key_downs++;
                } else if (in[i].mi.dwFlags & MOUSEEVENTF_WHEEL) {
                    wheel_events++;
                }
            }
        };
    }
};

// Raw value at a pull depth: exactly at it (the first raw value that reaches it)
// or just below it.
uint16_t raw_at(float pull)    { return static_cast<uint16_t>(std::ceil(pull * 32767.0f)); }
uint16_t raw_below(float pull) { return static_cast<uint16_t>(raw_at(pull) - 1); }
// Raw value of the Hold release point (pull <= depth - gap) and just above it.
uint16_t raw_release(float pull) {
    return static_cast<uint16_t>(std::floor((pull - windeck::config::kTriggerReleaseGap) * 32767.0f));
}

constexpr uint16_t kFull = 32767;

TriggerBinding key_binding(uint16_t vk, float pull, BindingMode mode = BindingMode::Hold,
                           ClickStrength click = ClickStrength::Low) {
    return {{ActionType::Keyboard, vk}, pull, mode, click};
}

TriggerConfig buttons(TriggerBinding a, TriggerBinding b = {}, TriggerBinding c = {}) {
    TriggerConfig t;
    t.mode = TriggerMode::Buttons;
    t.bindings = {a, b, c};
    return t;
}

struct Rig {
    Capture cap;
    SyntheticInput synth{cap.sender()};
    TriggerMapper trigger{HoldSource::RightTrigger};

    explicit Rig(const TriggerConfig& cfg) {
        trigger.apply_config(cfg);
        update(0, 0);   // a released trigger arms it
    }
    TriggerFrame update(uint16_t raw, int ms) {
        auto f = trigger.update(raw, at(ms), synth);
        synth.reconcile_holds();
        synth.flush();
        return f;
    }
    TriggerFrame tick(int ms) {
        auto f = trigger.tick(at(ms), synth);
        synth.reconcile_holds();
        synth.flush();
        return f;
    }
};

} // namespace

TEST_CASE("Xbox mode passes the analog trigger through its dead zone", "[trigger]") {
    Rig rig{TriggerConfig{}};
    CHECK(rig.update(0, 1).axis == 0);
    CHECK(rig.update(kFull, 2).axis == 255);
    auto f = rig.update(raw_at(0.5f), 3);
    CHECK(f.axis > 100);
    CHECK(f.axis < 155);
    CHECK(f.xbox_buttons == 0);
    CHECK(f.click == ClickStrength::None);
}

TEST_CASE("Buttons mode sends no analog value", "[trigger]") {
    Rig rig{buttons(key_binding('W', 0.3f))};
    CHECK(rig.update(kFull, 1).axis == 0);
}

TEST_CASE("A Hold binding presses at its depth and releases 5 points below", "[trigger]") {
    Rig rig{buttons(key_binding('W', 0.3f))};

    auto f = rig.update(raw_below(0.3f), 1);
    CHECK_FALSE(rig.synth.key_down('W'));
    CHECK(f.click == ClickStrength::None);

    f = rig.update(raw_at(0.3f), 2);
    CHECK(rig.synth.key_down('W'));
    CHECK(f.click == ClickStrength::Low);

    f = rig.update(static_cast<uint16_t>(raw_release(0.3f) + 1), 3);   // inside the gap
    CHECK(rig.synth.key_down('W'));
    CHECK(f.click == ClickStrength::None);

    f = rig.update(raw_release(0.3f), 4);
    CHECK_FALSE(rig.synth.key_down('W'));
    CHECK(f.click == ClickStrength::Low);   // Hold clicks on release too
}

TEST_CASE("Jitter inside the release gap gives one press", "[trigger]") {
    Rig rig{buttons(key_binding('W', 0.3f))};
    for (int i = 0; i < 10; i++) {
        rig.update(raw_at(0.31f), 10 + 2 * i);
        rig.update(raw_at(0.27f), 11 + 2 * i);
    }
    CHECK(rig.synth.key_down('W'));
    CHECK(rig.cap.key_downs == 1);
    CHECK(rig.cap.key_ups == 0);
}

TEST_CASE("Depths 10% and 100% work at the edges of the raw range", "[trigger]") {
    Rig rig{buttons(key_binding('A', 0.10f), key_binding('B', 1.0f))};
    rig.update(raw_at(0.10f), 1);
    CHECK(rig.synth.key_down('A'));
    rig.update(kFull - 1, 2);
    CHECK_FALSE(rig.synth.key_down('B'));
    rig.update(kFull, 3);
    CHECK(rig.synth.key_down('B'));
    rig.update(0, 4);
    CHECK_FALSE(rig.synth.key_down('A'));
    CHECK_FALSE(rig.synth.key_down('B'));
}

TEST_CASE("Bindings are independent: any order of depths, one jump past all", "[trigger]") {
    // B below A, C sharing B's depth.
    TriggerConfig cfg = buttons(key_binding('A', 0.6f), key_binding('B', 0.3f),
                                {{ActionType::MouseButton, 0}, 0.3f, BindingMode::Hold, ClickStrength::Low});
    Rig rig{cfg};

    rig.update(kFull, 1);   // one report from rest to full
    CHECK(rig.synth.key_down('A'));
    CHECK(rig.synth.key_down('B'));
    CHECK(rig.synth.mouse_down(0));

    rig.update(raw_at(0.45f), 2);   // below A's release point, above B's and C's
    CHECK_FALSE(rig.synth.key_down('A'));
    CHECK(rig.synth.key_down('B'));
    CHECK(rig.synth.mouse_down(0));
}

TEST_CASE("At most one click per report: the strongest", "[trigger]") {
    Rig rig{buttons(key_binding('A', 0.3f, BindingMode::Hold, ClickStrength::Low),
                    key_binding('B', 0.5f, BindingMode::Hold, ClickStrength::High),
                    key_binding('C', 0.7f, BindingMode::Hold, ClickStrength::None))};
    CHECK(rig.update(kFull, 1).click == ClickStrength::High);

    // C alone presses, and its click is None.
    Rig quiet{buttons(key_binding('C', 0.7f, BindingMode::Hold, ClickStrength::None))};
    CHECK(quiet.update(kFull, 1).click == ClickStrength::None);
    CHECK(quiet.synth.key_down('C'));
}

TEST_CASE("A None binding never presses or clicks", "[trigger]") {
    TriggerConfig cfg;
    cfg.mode = TriggerMode::Buttons;   // all three bindings default to None
    Rig rig{cfg};
    auto f = rig.update(kFull, 1);
    CHECK(f.click == ClickStrength::None);
    CHECK(f.xbox_buttons == 0);
    CHECK(rig.cap.key_downs == 0);
}

TEST_CASE("An Immediate binding taps once for 50 ms and does nothing on release", "[trigger]") {
    Rig rig{buttons(key_binding('E', 0.5f, BindingMode::Immediate))};

    auto f = rig.update(kFull, 10);
    CHECK(rig.synth.key_down('E'));
    CHECK(f.click == ClickStrength::Low);
    REQUIRE(rig.trigger.next_deadline().has_value());
    CHECK(*rig.trigger.next_deadline() == at(60));

    rig.update(kFull, 40);
    CHECK(rig.synth.key_down('E'));
    CHECK(rig.tick(59).click == ClickStrength::None);
    CHECK(rig.synth.key_down('E'));

    f = rig.tick(60);                 // the tap ends without a report
    CHECK_FALSE(rig.synth.key_down('E'));
    CHECK(f.click == ClickStrength::None);
    CHECK_FALSE(rig.trigger.next_deadline().has_value());

    rig.update(kFull, 100);           // still pulled: no second tap
    CHECK_FALSE(rig.synth.key_down('E'));

    f = rig.update(raw_below(0.5f), 120);   // eased back: no action, no click
    CHECK(f.click == ClickStrength::None);
    CHECK(rig.cap.key_downs == 1);

    f = rig.update(raw_at(0.5f), 130);      // no gap: just below then at the depth taps again
    CHECK(rig.synth.key_down('E'));
    CHECK(f.click == ClickStrength::Low);
    CHECK(rig.cap.key_downs == 2);
}

TEST_CASE("A quick pull released after 4 ms completes its tap and re-arms at its end", "[trigger]") {
    Rig rig{buttons(key_binding('E', 0.5f, BindingMode::Immediate))};
    rig.update(kFull, 0);
    rig.update(0, 4);
    CHECK(rig.synth.key_down('E'));    // the tap still runs
    rig.tick(50);
    CHECK_FALSE(rig.synth.key_down('E'));
    rig.update(kFull, 54);             // re-armed when the tap ended: taps again
    CHECK(rig.synth.key_down('E'));
    CHECK(rig.cap.key_downs == 2);
}

TEST_CASE("A tap ending with the pull still past the depth does not re-arm", "[trigger]") {
    Rig rig{buttons(key_binding('E', 0.5f, BindingMode::Immediate))};
    rig.update(kFull, 0);
    rig.update(kFull, 4);
    rig.tick(50);
    rig.update(kFull, 60);
    CHECK(rig.cap.key_downs == 1);
    rig.update(raw_below(0.5f), 70);
    rig.update(kFull, 80);
    CHECK(rig.cap.key_downs == 2);
}

TEST_CASE("A report exactly at the deadline is handled after the expiry", "[trigger]") {
    Rig rig{buttons(key_binding('E', 0.5f, BindingMode::Immediate))};
    rig.update(kFull, 0);
    rig.update(0, 50);                 // expiry first, then this below-depth report
    CHECK_FALSE(rig.synth.key_down('E'));
    rig.update(kFull, 54);
    CHECK(rig.cap.key_downs == 2);
}

TEST_CASE("A tap starting as another on the same key ends gives no up-down pair", "[trigger]") {
    Rig rig{buttons(key_binding('E', 0.3f, BindingMode::Immediate),
                    key_binding('E', 0.6f, BindingMode::Immediate))};
    rig.update(raw_at(0.4f), 0);       // A taps E until 50
    rig.update(raw_at(0.7f), 50);      // A's tap ends and B's starts in the same pass
    CHECK(rig.synth.key_down('E'));
    CHECK(rig.cap.key_downs == 1);
    CHECK(rig.cap.key_ups == 0);
    rig.tick(100);
    CHECK_FALSE(rig.synth.key_down('E'));
    CHECK(rig.cap.key_ups == 1);
}

TEST_CASE("A tap on an already-held key adds no press but keeps it to the tap's end", "[trigger]") {
    Rig rig{buttons(key_binding('E', 0.3f, BindingMode::Hold),
                    key_binding('E', 0.6f, BindingMode::Immediate))};
    rig.update(raw_at(0.7f), 0);       // both want E
    rig.update(raw_at(0.2f), 10);      // Hold released; the tap still runs to 50
    CHECK(rig.synth.key_down('E'));
    rig.tick(50);
    CHECK_FALSE(rig.synth.key_down('E'));
    CHECK(rig.cap.key_downs == 1);
}

TEST_CASE("A wheel binding ticks once per press, in either mode", "[trigger]") {
    for (auto mode : {BindingMode::Hold, BindingMode::Immediate}) {
        Rig rig{buttons({{ActionType::MouseScroll, 1}, 0.5f, mode, ClickStrength::Low})};
        rig.update(kFull, 10);
        rig.update(kFull, 14);
        rig.tick(70);
        rig.update(0, 80);
        rig.update(kFull, 90);
        CHECK(rig.cap.wheel_events == 2);
    }
}

TEST_CASE("A trigger stays disarmed until released after start, profile change or menu", "[trigger]") {
    Capture cap;
    SyntheticInput synth{cap.sender()};
    TriggerMapper trigger{HoldSource::LeftTrigger};
    trigger.apply_config(buttons(key_binding('W', 0.3f), key_binding('S', 0.7f)));

    // Held at start-up, including between the two depths.
    trigger.update(kFull, at(0), synth);
    trigger.update(raw_at(0.5f), at(4), synth);
    synth.reconcile_holds();
    CHECK_FALSE(synth.key_down('W'));
    CHECK_FALSE(trigger.armed());

    trigger.update(raw_below(0.05f), at(8), synth);   // released to 5% or less: arms
    CHECK(trigger.armed());
    trigger.update(kFull, at(12), synth);
    synth.reconcile_holds();
    CHECK(synth.key_down('W'));

    // The menu opens while it is held: forced release, then still pulled.
    trigger.release_all(synth);
    synth.reconcile_holds();
    CHECK_FALSE(synth.key_down('W'));
    auto f = trigger.update(kFull, at(20), synth);
    synth.reconcile_holds();
    CHECK_FALSE(synth.key_down('W'));
    CHECK(f.click == ClickStrength::None);

    // A new config disarms too.
    trigger.update(0, at(24), synth);
    REQUIRE(trigger.armed());
    trigger.apply_config(buttons(key_binding('W', 0.3f)));
    CHECK_FALSE(trigger.armed());
}

TEST_CASE("A forced release drops outputs and running taps, without clicks", "[trigger]") {
    Rig rig{buttons(key_binding('W', 0.3f),
                    key_binding('E', 0.5f, BindingMode::Immediate))};
    rig.update(kFull, 0);
    REQUIRE(rig.synth.key_down('W'));
    REQUIRE(rig.synth.key_down('E'));

    rig.trigger.release_all(rig.synth);
    rig.synth.reconcile_holds();
    CHECK_FALSE(rig.synth.key_down('W'));
    CHECK_FALSE(rig.synth.key_down('E'));
    CHECK_FALSE(rig.trigger.next_deadline().has_value());
    CHECK(rig.tick(100).click == ClickStrength::None);
}

TEST_CASE("Xbox bindings hold their bit while held or during the tap", "[trigger]") {
    Rig rig{buttons({{ActionType::XboxButton, 0x1000}, 0.3f, BindingMode::Hold, ClickStrength::Low},
                    {{ActionType::XboxButton, 0x2000}, 0.6f, BindingMode::Immediate, ClickStrength::Low})};
    CHECK(rig.update(kFull, 0).xbox_buttons == 0x3000);
    CHECK(rig.tick(50).xbox_buttons == 0x1000);
    CHECK(rig.update(0, 60).xbox_buttons == 0);
}

TEST_CASE("The Xbox state keeps each source's bits apart", "[trigger][xbox_state]") {
    using windeck::output::XboxSource;
    windeck::output::XboxState x;
    x.set_buttons(XboxSource::Buttons, 0x2000);         // a physical B
    x.set_buttons(XboxSource::RightTrigger, 0x3000);    // a trigger on A and B
    x.set_left_stick(1000, -2000);
    x.set_right_trigger(200);
    CHECK(x.report().wButtons == 0x3000);

    // The trigger's tap ends between reports: only its bits change.
    x.set_buttons(XboxSource::RightTrigger, 0);
    auto r = x.report();
    CHECK(r.wButtons == 0x2000);
    CHECK(r.sThumbLX == 1000);
    CHECK(r.sThumbLY == -2000);
    CHECK(r.bRightTrigger == 200);

    x.clear();
    r = x.report();
    CHECK(r.wButtons == 0);
    CHECK(r.sThumbLX == 0);
}
