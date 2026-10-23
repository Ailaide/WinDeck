#include "output/synthetic_input.h"
#include "mapper/button_mapper.h"
#include "mapper/stick_mapper.h"
#include "mapper/trigger_mapper.h"
#include "modes/radial_menu_mode.h"
#include "neptune/neptune_report.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using windeck::config::ActionType;
using windeck::config::ButtonAction;
using windeck::config::ButtonId;
using windeck::output::HoldSet;
using windeck::output::HoldSource;
using windeck::output::SyntheticInput;

// Every SyntheticInput here sends into a Capture instead of SendInput, so no
// test ever presses a real key or mouse button.

namespace {

struct Event {
    bool keyboard = false;
    uint16_t vk = 0;       // keyboard: the key; mouse: 0
    bool up = false;       // keyboard: key-up
    DWORD mouse_flags = 0; // mouse: MOUSEEVENTF_*
    LONG wheel = 0;        // mouse: wheel delta
};

struct Capture {
    std::vector<Event> events;

    SyntheticInput::Sender sender() {
        return [this](const INPUT* inputs, UINT count) {
            for (UINT i = 0; i < count; i++) {
                const INPUT& in = inputs[i];
                Event e;
                if (in.type == INPUT_KEYBOARD) {
                    e.keyboard = true;
                    e.up = (in.ki.dwFlags & KEYEVENTF_KEYUP) != 0;
                    e.vk = in.ki.wVk != 0
                        ? in.ki.wVk
                        : static_cast<uint16_t>(MapVirtualKeyW(in.ki.wScan, MAPVK_VSC_TO_VK));
                } else {
                    e.mouse_flags = in.mi.dwFlags;
                    e.wheel = static_cast<LONG>(in.mi.mouseData);
                }
                events.push_back(e);
            }
        };
    }

    int count_key(uint16_t vk, bool up) const {
        int n = 0;
        for (const auto& e : events) n += (e.keyboard && e.vk == vk && e.up == up);
        return n;
    }
    int count_mouse(DWORD flag) const {
        int n = 0;
        for (const auto& e : events) n += (!e.keyboard && (e.mouse_flags & flag) != 0);
        return n;
    }
};

HoldSet keys(std::initializer_list<uint16_t> vks) {
    HoldSet h;
    for (auto vk : vks) h.add_key(vk);
    return h;
}

HoldSet mouse(uint8_t index) {
    HoldSet h;
    h.add_mouse(index);
    return h;
}

void step(SyntheticInput& synth) {
    synth.reconcile_holds();
    synth.flush();
}

constexpr uint16_t kW = 'W';

} // namespace

TEST_CASE("A key shared by two sources stays down until both let go", "[holds]") {
    Capture cap;
    SyntheticInput synth(cap.sender());

    synth.set_holds(HoldSource::Buttons, keys({kW}));
    synth.set_holds(HoldSource::LeftStick, keys({kW}));
    step(synth);
    CHECK(synth.key_down(kW));
    CHECK(cap.count_key(kW, false) == 1);

    synth.clear_holds(HoldSource::Buttons);
    step(synth);
    CHECK(synth.key_down(kW));
    CHECK(cap.count_key(kW, true) == 0);

    synth.clear_holds(HoldSource::LeftStick);
    step(synth);
    CHECK_FALSE(synth.key_down(kW));
    CHECK(cap.count_key(kW, true) == 1);
    CHECK(cap.count_key(kW, false) == 1);
}

TEST_CASE("A source that is not updated keeps its set", "[holds]") {
    Capture cap;
    SyntheticInput synth(cap.sender());

    synth.set_holds(HoldSource::LeftPadClick, mouse(0));
    step(synth);
    REQUIRE(synth.mouse_down(0));

    // Other sources run and replace their own sets; the click source does not.
    synth.set_holds(HoldSource::Buttons, HoldSet{});
    synth.set_holds(HoldSource::RightStick, keys({'D'}));
    step(synth);
    synth.set_holds(HoldSource::RightStick, HoldSet{});
    step(synth);

    CHECK(synth.mouse_down(0));
    CHECK(cap.count_mouse(MOUSEEVENTF_LEFTDOWN) == 1);
    CHECK(cap.count_mouse(MOUSEEVENTF_LEFTUP) == 0);
}

TEST_CASE("Replacing a set releases the old key before pressing the new one", "[holds]") {
    Capture cap;
    SyntheticInput synth(cap.sender());

    synth.set_holds(HoldSource::Buttons, keys({'A'}));
    step(synth);
    cap.events.clear();

    synth.set_holds(HoldSource::Buttons, keys({'B'}));
    step(synth);

    REQUIRE(cap.events.size() == 2);
    CHECK(cap.events[0].vk == 'A');
    CHECK(cap.events[0].up);
    CHECK(cap.events[1].vk == 'B');
    CHECK_FALSE(cap.events[1].up);
}

TEST_CASE("A modifier goes down before the key it modifies and up after it", "[holds]") {
    Capture cap;
    SyntheticInput synth(cap.sender());

    // Ctrl from one source, Tab and a click from others, in the same pass.
    // Tab (0x09) sorts below Ctrl (0x11) by key code; the order must not follow that.
    synth.set_holds(HoldSource::Buttons, keys({VK_CONTROL}));
    synth.set_holds(HoldSource::RightPadClick, keys({VK_TAB}));
    synth.set_holds(HoldSource::LeftPadClick, mouse(0));
    step(synth);

    REQUIRE(cap.events.size() == 3);
    CHECK(cap.events[0].keyboard);
    CHECK(cap.events[0].vk == VK_CONTROL);
    CHECK_FALSE(cap.events[0].up);
    CHECK(cap.events[1].vk == VK_TAB);
    CHECK_FALSE(cap.events[1].up);
    CHECK_FALSE(cap.events[2].keyboard);
    CHECK((cap.events[2].mouse_flags & MOUSEEVENTF_LEFTDOWN) != 0);

    cap.events.clear();
    synth.clear_holds(HoldSource::Buttons);
    synth.clear_holds(HoldSource::RightPadClick);
    synth.clear_holds(HoldSource::LeftPadClick);
    step(synth);

    REQUIRE(cap.events.size() == 3);
    CHECK((cap.events[0].mouse_flags & MOUSEEVENTF_LEFTUP) != 0);
    CHECK(cap.events[1].vk == VK_TAB);
    CHECK(cap.events[1].up);
    CHECK(cap.events[2].vk == VK_CONTROL);
    CHECK(cap.events[2].up);
}

TEST_CASE("A mouse button shared by two sources stays down until both let go", "[holds]") {
    Capture cap;
    SyntheticInput synth(cap.sender());

    synth.set_holds(HoldSource::Buttons, mouse(0));
    synth.set_holds(HoldSource::RightPadClick, mouse(0));
    step(synth);
    synth.clear_holds(HoldSource::RightPadClick);
    step(synth);
    CHECK(synth.mouse_down(0));

    synth.clear_holds(HoldSource::Buttons);
    step(synth);
    CHECK_FALSE(synth.mouse_down(0));
    CHECK(cap.count_mouse(MOUSEEVENTF_LEFTDOWN) == 1);
    CHECK(cap.count_mouse(MOUSEEVENTF_LEFTUP) == 1);
}

TEST_CASE("release_all lifts everything and forgets every set", "[holds]") {
    Capture cap;
    SyntheticInput synth(cap.sender());

    synth.set_holds(HoldSource::Buttons, keys({kW, 'E'}));
    synth.set_holds(HoldSource::LeftPadClick, mouse(1));
    step(synth);

    synth.release_all();
    CHECK_FALSE(synth.key_down(kW));
    CHECK_FALSE(synth.key_down('E'));
    CHECK_FALSE(synth.mouse_down(1));

    // Nothing declared since: a later reconcile must not bring anything back.
    cap.events.clear();
    step(synth);
    step(synth);
    CHECK(cap.events.empty());
}

TEST_CASE("Wheel ticks are one-off and add up within a frame", "[holds]") {
    Capture cap;
    SyntheticInput synth(cap.sender());

    synth.mouse_scroll(WHEEL_DELTA);
    synth.mouse_scroll(-WHEEL_DELTA);
    synth.flush();
    CHECK(cap.count_mouse(MOUSEEVENTF_WHEEL) == 0);

    synth.mouse_scroll(WHEEL_DELTA);
    synth.mouse_scroll(WHEEL_DELTA);
    synth.flush();
    REQUIRE(cap.count_mouse(MOUSEEVENTF_WHEEL) == 1);
    CHECK(cap.events.back().wheel == 2 * WHEEL_DELTA);

    synth.flush();
    CHECK(cap.count_mouse(MOUSEEVENTF_WHEEL) == 1);
}

TEST_CASE("Hold sets ignore keys and buttons that cannot be held", "[holds]") {
    HoldSet h;
    h.add_key(0);
    h.add_key(256);
    h.add_mouse(3);
    h.add(ButtonAction{ActionType::XboxButton, 0x1000});
    h.add(ButtonAction{ActionType::MouseScroll, 1});
    h.add(ButtonAction{ActionType::None, 0});
    h.add(ButtonAction{ActionType::MouseButton, 3});
    CHECK(h.empty());

    h.add(ButtonAction{ActionType::Keyboard, kW});
    h.add(ButtonAction{ActionType::MouseButton, 2});
    CHECK(h.keys.test(kW));
    CHECK(h.mouse.test(2));
}

// --- The real producers sharing outputs ---

namespace {

std::array<ButtonAction, static_cast<size_t>(ButtonId::Count)> no_bindings() {
    std::array<ButtonAction, static_cast<size_t>(ButtonId::Count)> a;
    a.fill(ButtonAction{ActionType::None, 0});
    return a;
}

void bind(std::array<ButtonAction, static_cast<size_t>(ButtonId::Count)>& a,
          ButtonId id, ButtonAction action) {
    a[static_cast<size_t>(id)] = action;
}

USHORT map_buttons(windeck::mapper::ButtonMapper& m, uint32_t nb, SyntheticInput& synth) {
    windeck::mapper::SystemButtonEvents sys;
    return m.map(nb, 0, sys, synth);
}

} // namespace

TEST_CASE("A button and a WASD stick share W", "[holds][mappers]") {
    using namespace windeck;
    Capture cap;
    SyntheticInput synth(cap.sender());

    mapper::ButtonMapper buttons;
    auto actions = no_bindings();
    bind(actions, ButtonId::A, {ActionType::Keyboard, kW});
    buttons.set_config(actions);

    mapper::StickMapper stick{HoldSource::LeftStick};
    config::StickConfig cfg;
    cfg.mode = config::StickMode::Wasd;
    stick.apply_config(cfg);
    int16_t x = 0, y = 0;

    // Both hold W.
    map_buttons(buttons, neptune::btn::kA, synth);
    stick.map(0, 32767, x, y, synth);
    step(synth);
    REQUIRE(synth.key_down(kW));

    // The button lets go; the stick still holds W.
    map_buttons(buttons, 0, synth);
    stick.map(0, 32767, x, y, synth);
    step(synth);
    CHECK(synth.key_down(kW));
    CHECK(cap.count_key(kW, true) == 0);

    // The stick centres too: W goes up exactly once.
    map_buttons(buttons, 0, synth);
    stick.map(0, 0, x, y, synth);
    step(synth);
    CHECK_FALSE(synth.key_down(kW));
    CHECK(cap.count_key(kW, true) == 1);
    CHECK(cap.count_key(kW, false) == 1);
}

TEST_CASE("Releasing one mapper never lifts a key another still holds", "[holds][mappers]") {
    using namespace windeck;
    Capture cap;
    SyntheticInput synth(cap.sender());

    mapper::ButtonMapper buttons;
    auto actions = no_bindings();
    bind(actions, ButtonId::B, {ActionType::Keyboard, kW});
    buttons.set_config(actions);

    mapper::StickMapper stick{HoldSource::RightStick};
    config::StickConfig cfg;
    cfg.mode = config::StickMode::Wasd;
    stick.apply_config(cfg);
    int16_t x = 0, y = 0;

    map_buttons(buttons, neptune::btn::kB, synth);
    stick.map(0, 32767, x, y, synth);
    step(synth);

    buttons.release_all(synth);
    step(synth);
    CHECK(synth.key_down(kW));

    stick.release_all(synth);
    step(synth);
    CHECK_FALSE(synth.key_down(kW));
}

TEST_CASE("The radial menu holds only the hovered sector's key", "[holds][mappers]") {
    using namespace windeck;
    Capture cap;
    SyntheticInput synth(cap.sender());

    modes::RadialMenuMode radial{HoldSource::LeftPadRadial};
    std::array<config::ButtonAction, 8> keys{};
    for (int i = 0; i < 8; i++) keys[i] = {ActionType::Keyboard, static_cast<uint16_t>('1' + i)};
    radial.set_keys(keys);

    // A button also holds '1'.
    synth.set_holds(HoldSource::Buttons, [] { HoldSet h; h.add_key('1'); return h; }());

    radial.update(0.0f, 0.9f, synth);   // north: sector 0 -> '1'
    step(synth);
    CHECK(synth.key_down('1'));

    radial.update(0.9f, 0.0f, synth);   // east: sector 2 -> '3'
    step(synth);
    CHECK(synth.key_down('3'));
    CHECK(synth.key_down('1'));          // still held by the button

    // Once the button lets go, nothing holds '1': the radial menu kept only '3'.
    synth.clear_holds(HoldSource::Buttons);
    step(synth);
    CHECK_FALSE(synth.key_down('1'));
    CHECK(synth.key_down('3'));

    radial.release_all(synth);
    step(synth);
    CHECK_FALSE(synth.key_down('3'));
}

TEST_CASE("A trigger binding's key is shared with a button", "[holds][mappers]") {
    using namespace windeck;
    Capture cap;
    SyntheticInput synth(cap.sender());

    mapper::TriggerMapper trigger{HoldSource::RightTrigger};
    config::TriggerConfig tc;
    tc.mode = config::TriggerMode::Buttons;
    tc.bindings[0] = {{ActionType::Keyboard, 'E'}, 0.5f, config::BindingMode::Hold,
                      config::ClickStrength::Low};
    trigger.apply_config(tc);
    const auto t0 = mapper::TriggerMapper::Clock::time_point{};
    trigger.update(0, t0, synth);   // a released trigger arms it

    mapper::ButtonMapper buttons;
    auto actions = no_bindings();
    bind(actions, ButtonId::X, {ActionType::Keyboard, 'E'});
    buttons.set_config(actions);

    map_buttons(buttons, neptune::btn::kX, synth);
    trigger.update(32767, t0, synth);
    step(synth);
    REQUIRE(synth.key_down('E'));

    // The trigger eases off; the button still holds E.
    map_buttons(buttons, neptune::btn::kX, synth);
    trigger.update(0, t0, synth);
    step(synth);
    CHECK(synth.key_down('E'));
    CHECK(cap.count_key('E', true) == 0);
}

TEST_CASE("A wheel binding ticks once per press, not while held", "[holds][mappers]") {
    using namespace windeck;
    Capture cap;
    SyntheticInput synth(cap.sender());

    mapper::ButtonMapper buttons;
    auto actions = no_bindings();
    bind(actions, ButtonId::Y, {ActionType::MouseScroll, 1});
    buttons.set_config(actions);

    map_buttons(buttons, 0, synth);                 // first frame: nothing held
    step(synth);
    map_buttons(buttons, neptune::btn::kY, synth);  // press
    step(synth);
    map_buttons(buttons, neptune::btn::kY, synth);  // still held
    step(synth);

    CHECK(cap.count_mouse(MOUSEEVENTF_WHEEL) == 1);
}

TEST_CASE("After the menu, a button held through it is no press; a fresh one is", "[holds][mappers]") {
    using namespace windeck;
    Capture cap;
    SyntheticInput synth(cap.sender());

    mapper::ButtonMapper buttons;
    auto actions = no_bindings();
    bind(actions, ButtonId::Y, {ActionType::MouseScroll, 1});
    buttons.set_config(actions);
    map_buttons(buttons, 0, synth);                 // first frame: nothing held
    step(synth);

    // The menu opens (forced release) and resyncs from each report it takes:
    // Y is held in its last one. The first report after the menu still holds Y.
    buttons.release_all(synth);
    buttons.resync(neptune::btn::kY, 0);
    map_buttons(buttons, neptune::btn::kY, synth);
    step(synth);
    CHECK(cap.count_mouse(MOUSEEVENTF_WHEEL) == 0);

    // Y is let go inside the menu and pressed in the first report after it.
    buttons.release_all(synth);
    buttons.resync(0, 0);
    map_buttons(buttons, neptune::btn::kY, synth);
    step(synth);
    CHECK(cap.count_mouse(MOUSEEVENTF_WHEEL) == 1);
}
