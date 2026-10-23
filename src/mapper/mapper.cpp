#include "mapper/mapper.h"
#include "overlay/radial_overlay.h"
#include "mapper/dead_zone.h"
#include "neptune/neptune_report.h"
#include <cstring>

namespace windeck::mapper {

using windeck::output::XboxSource;

void Mapper::set_overlay(windeck::overlay::RadialOverlay* overlay) {
    std::unique_lock lock(mutex_);
    left_touchpad_.set_overlay(overlay);
    right_touchpad_.set_overlay(overlay);
}

void Mapper::set_device(windeck::neptune::NeptuneDevice* device) {
    left_touchpad_.set_haptic_device(device, windeck::neptune::HapticSide::Left);
    right_touchpad_.set_haptic_device(device, windeck::neptune::HapticSide::Right);
}

void Mapper::apply_profile(const config::Profile& profile,
                           windeck::output::SyntheticInput& synth) {
    std::unique_lock lock(mutex_);
    release_all_unlocked(synth);
    synth.flush();
    button_mapper_.set_config(profile.buttons);

    left_stick_.apply_config(profile.left_stick);
    right_stick_.apply_config(profile.right_stick);

    // A new trigger config disarms the trigger: a binding the new profile adds
    // never fires on its own while the trigger is already pulled.
    left_trigger_.apply_config(profile.left_trigger);
    right_trigger_.apply_config(profile.right_trigger);

    dpad_.apply_config(profile.dpad);

    left_touchpad_.set_name("L-Pad");
    left_touchpad_.apply_config(profile.left_touchpad);
    right_touchpad_.set_name("R-Pad");
    right_touchpad_.apply_config(profile.right_touchpad);

    gyro_.apply_config(profile.gyro);
}

MapResult Mapper::map(const neptune::InputReport& input,
                       windeck::output::SyntheticInput& synth) {
    std::shared_lock lock(mutex_);
    return map_unlocked(input, synth, Clock::now());
}

MapResult Mapper::map_unlocked(const neptune::InputReport& input,
                                windeck::output::SyntheticInput& synth,
                                Clock::time_point now) {
    MapResult result;

    // Buttons
    xbox_.set_buttons(XboxSource::Buttons, button_mapper_.map(
        input.buttons, input.buttons_ext, result.sys_events, synth));

    // D-Pad
    xbox_.set_buttons(XboxSource::Dpad, dpad_.map(input.buttons, synth));

    // Sticks
    int16_t lx = 0, ly = 0, rx = 0, ry = 0;
    left_stick_.map(input.left_stick_x, input.left_stick_y, lx, ly, synth);
    right_stick_.map(input.right_stick_x, input.right_stick_y, rx, ry, synth);
    xbox_.set_left_stick(lx, ly);
    xbox_.set_right_stick(rx, ry);

    // Triggers
    const TriggerFrame lt = left_trigger_.update(input.left_trigger, now, synth);
    const TriggerFrame rt = right_trigger_.update(input.right_trigger, now, synth);
    xbox_.set_left_trigger(lt.axis);
    xbox_.set_right_trigger(rt.axis);
    xbox_.set_buttons(XboxSource::LeftTrigger, lt.xbox_buttons);
    xbox_.set_buttons(XboxSource::RightTrigger, rt.xbox_buttons);
    result.clicks = {lt.click, rt.click};

    // Touchpads — coordinates reset to (0,0) when finger lifts (verified on hardware)
    bool touching_left  = (input.left_pad_x != 0 || input.left_pad_y != 0);
    bool touching_right = (input.right_pad_x != 0 || input.right_pad_y != 0);

    left_touchpad_.update(input.left_pad_x, input.left_pad_y,
                           input.left_pad_pressure, touching_left, synth);
    right_touchpad_.update(input.right_pad_x, input.right_pad_y,
                            input.right_pad_pressure, touching_right, synth);

    // Gyro
    gyro_.update(input.gyro_pitch, input.gyro_yaw, input.gyro_roll, synth);

    result.xbox_report = xbox_.report();
    return result;
}

MapResult Mapper::map_and_flush(const neptune::InputReport& input,
                                windeck::output::SyntheticInput& synth) {
    std::shared_lock lock(mutex_);
    MapResult result = map_unlocked(input, synth, Clock::now());
    left_touchpad_.tick_inertia(synth);
    right_touchpad_.tick_inertia(synth);
    synth.reconcile_holds();   // one press/release per key and mouse button across all sources
    synth.flush();
    return result;
}

void Mapper::tick(windeck::output::SyntheticInput& synth) {
    std::shared_lock lock(mutex_);
    tick_unlocked(synth, Clock::now());
}

XUSB_REPORT Mapper::tick_and_flush(windeck::output::SyntheticInput& synth) {
    std::shared_lock lock(mutex_);
    tick_unlocked(synth, Clock::now());
    // Only the triggers' sets can have changed (a tap ending); every other
    // source keeps its last set, so this never lifts another source's key.
    synth.reconcile_holds();
    synth.flush();
    return xbox_.report();
}

void Mapper::tick_unlocked(windeck::output::SyntheticInput& synth, Clock::time_point now) {
    left_touchpad_.tick_inertia(synth);
    right_touchpad_.tick_inertia(synth);
    // Taps whose 50 ms are up end now, even without a report; the other
    // sources' Xbox contributions stay as the last report left them.
    xbox_.set_buttons(XboxSource::LeftTrigger, left_trigger_.tick(now, synth).xbox_buttons);
    xbox_.set_buttons(XboxSource::RightTrigger, right_trigger_.tick(now, synth).xbox_buttons);
}

std::optional<Mapper::Clock::time_point> Mapper::next_deadline() const {
    std::shared_lock lock(mutex_);
    auto l = left_trigger_.next_deadline();
    auto r = right_trigger_.next_deadline();
    if (l && r) return (*l < *r) ? l : r;
    return l ? l : r;
}

void Mapper::resync_buttons(uint32_t buttons, uint32_t buttons_ext) {
    std::unique_lock lock(mutex_);
    button_mapper_.resync(buttons, buttons_ext);
}

void Mapper::release_all(windeck::output::SyntheticInput& synth) {
    // The whole forced release, sending included, happens under the exclusive
    // lock, so a profile change from another thread never touches the hold sets
    // at the same time.
    std::unique_lock lock(mutex_);
    release_all_unlocked(synth);
    synth.release_all();
}

void Mapper::release_all_unlocked(windeck::output::SyntheticInput& synth) {
    button_mapper_.release_all(synth);
    left_stick_.release_all(synth);
    right_stick_.release_all(synth);
    dpad_.release_all(synth);
    left_touchpad_.release_all(synth);
    right_touchpad_.release_all(synth);
    left_trigger_.release_all(synth);
    right_trigger_.release_all(synth);
    xbox_.clear();
    // Every source's set is now empty: queue the releases (sent on the next flush).
    synth.reconcile_holds();
}

} // namespace windeck::mapper
