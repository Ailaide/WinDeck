#include "modes/inertia.h"
#include "modes/tick_timer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <cmath>
#include <thread>

using windeck::modes::Inertia;
using windeck::modes::smooth_alpha;
using windeck::modes::TickTimer;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

// Longer than 8 reference ticks (32 ms), so TickTimer::step() hits its
// default clamp and returns exactly 8 regardless of scheduler jitter.
void stall() {
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
}

} // namespace

TEST_CASE("TickTimer starts with one reference tick", "[tick_timer]") {
    TickTimer timer;
    CHECK(timer.step() == 1.0f);

    timer.reset();
    CHECK(timer.step() == 1.0f);
}

TEST_CASE("TickTimer measures elapsed time in 250 Hz ticks", "[tick_timer]") {
    TickTimer timer;
    timer.step();
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    CHECK(timer.step(1000.0f) >= 5.0f);  // at least 20 ms = 5 ticks
}

TEST_CASE("TickTimer clamps long stalls", "[tick_timer]") {
    TickTimer timer;
    timer.step();
    stall();
    CHECK(timer.step() == 8.0f);

    stall();
    CHECK(timer.step(2.0f) == 2.0f);
}

TEST_CASE("smooth_alpha halves the distance once per half-life", "[tick_timer]") {
    CHECK(smooth_alpha(0.0f) == 0.0f);
    CHECK_THAT(smooth_alpha(1.0f), WithinAbs(0.5, 1e-6));
    CHECK_THAT(smooth_alpha(3.0f, 3.0f), WithinAbs(0.5, 1e-6));
    CHECK_THAT(smooth_alpha(2.0f, 1.0f), WithinAbs(0.75, 1e-6));
}

TEST_CASE("smooth_alpha converges at the same rate for any tick rate", "[tick_timer]") {
    // Four reference ticks with a half-life of two: 75% of the way to the
    // target, whether that time arrives as one step or as eight.
    auto substeps = GENERATE(1, 2, 4, 8);
    CAPTURE(substeps);

    const float step = 4.0f / static_cast<float>(substeps);
    float value = 0.0f;
    for (int i = 0; i < substeps; ++i) {
        value += (1.0f - value) * smooth_alpha(step, 2.0f);
    }
    CHECK_THAT(value, WithinAbs(0.75, 1e-5));
}

TEST_CASE("Inertia does not start below the cutoff", "[inertia]") {
    Inertia inertia;
    inertia.set_cutoff(0.5f);
    inertia.set_velocity(0.3f, 0.3f);  // magnitude ~0.42
    inertia.begin();
    CHECK_FALSE(inertia.is_active());

    float dx = 1.0f;
    float dy = 1.0f;
    CHECK_FALSE(inertia.tick(dx, dy));
    CHECK(dx == 0.0f);
    CHECK(dy == 0.0f);
}

TEST_CASE("Inertia glides from the release velocity and decays with time", "[inertia]") {
    const float friction = 0.9f;
    Inertia inertia;
    inertia.set_friction(friction);
    inertia.set_cutoff(0.01f);
    inertia.set_velocity(10.0f, -5.0f);
    inertia.begin();
    REQUIRE(inertia.is_active());

    float dx = 0.0f;
    float dy = 0.0f;

    // The first tick after release is one reference tick at full velocity.
    CHECK(inertia.tick(dx, dy));
    CHECK(dx == 10.0f);
    CHECK(dy == -5.0f);

    // A stall covers 8 reference ticks: the glide moves 8 ticks' worth at the
    // velocity left after one tick of friction...
    stall();
    CHECK(inertia.tick(dx, dy));
    CHECK_THAT(dx, WithinRel(10.0f * friction * 8.0f, 1e-5f));
    CHECK_THAT(dy, WithinRel(-5.0f * friction * 8.0f, 1e-5f));

    // ...and the velocity decays by friction^8, as 8 separate ticks would.
    stall();
    CHECK(inertia.tick(dx, dy));
    const float decayed = friction * std::pow(friction, 8.0f);
    CHECK_THAT(dx, WithinRel(10.0f * decayed * 8.0f, 1e-5f));
    CHECK_THAT(dy, WithinRel(-5.0f * decayed * 8.0f, 1e-5f));
}

TEST_CASE("Inertia stops once the velocity falls below the cutoff", "[inertia]") {
    Inertia inertia;
    inertia.set_friction(0.5f);
    inertia.set_cutoff(0.5f);
    inertia.set_velocity(0.9f, 0.0f);
    inertia.begin();
    REQUIRE(inertia.is_active());

    float dx = 0.0f;
    float dy = 0.0f;
    CHECK_FALSE(inertia.tick(dx, dy));  // moves this tick, then 0.45 < 0.5
    CHECK(dx == 0.9f);
    CHECK_FALSE(inertia.is_active());

    CHECK_FALSE(inertia.tick(dx, dy));
    CHECK(dx == 0.0f);
    CHECK(dy == 0.0f);
}

TEST_CASE("Inertia stop() halts the glide immediately", "[inertia]") {
    Inertia inertia;
    inertia.set_velocity(10.0f, 10.0f);
    inertia.begin();
    REQUIRE(inertia.is_active());

    inertia.stop();
    CHECK_FALSE(inertia.is_active());

    float dx = 1.0f;
    float dy = 1.0f;
    CHECK_FALSE(inertia.tick(dx, dy));
    CHECK(dx == 0.0f);
    CHECK(dy == 0.0f);
}

TEST_CASE("Inertia begin() restarts the timebase", "[inertia]") {
    Inertia inertia;
    inertia.set_friction(0.9f);
    inertia.set_cutoff(0.01f);
    inertia.set_velocity(10.0f, 0.0f);
    inertia.begin();

    float dx = 0.0f;
    float dy = 0.0f;
    inertia.tick(dx, dy);

    // A new release after a pause must not inherit the pause as elapsed time.
    stall();
    inertia.set_velocity(4.0f, 0.0f);
    inertia.begin();
    CHECK(inertia.tick(dx, dy));
    CHECK(dx == 4.0f);
}
