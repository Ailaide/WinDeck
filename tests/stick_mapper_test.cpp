#include "mapper/stick_mapper.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstdlib>

using windeck::config::ResponseCurve;
using windeck::config::StickConfig;
using windeck::config::StickMode;
using windeck::mapper::StickMapper;
using windeck::output::SyntheticInput;

// The keyboard and mouse modes queue input in SyntheticInput. These tests never
// call flush() or reconcile_holds(), so nothing is sent to the system.

namespace {

struct StickOut {
    int16_t x = 0;
    int16_t y = 0;
};

StickOut map_once(const StickConfig& cfg, int16_t raw_x, int16_t raw_y) {
    StickMapper mapper;
    SyntheticInput synth;
    mapper.apply_config(cfg);
    StickOut out;
    mapper.map(raw_x, raw_y, out.x, out.y, synth);
    return out;
}

} // namespace

TEST_CASE("Stick mode passes the stick through the dead zone", "[stick_mapper]") {
    StickConfig cfg;  // defaults: stick mode, 0.05 / 0.95, linear

    auto out = map_once(cfg, 32767, 0);
    CHECK(out.x == 32767);
    CHECK(out.y == 0);

    out = map_once(cfg, 1000, 1000);  // inside the inner dead zone
    CHECK(out.x == 0);
    CHECK(out.y == 0);
}

TEST_CASE("Stick mode applies the configured response curve", "[stick_mapper]") {
    StickConfig cfg;
    cfg.inner_deadzone = 0.0f;
    cfg.outer_deadzone = 1.0f;
    cfg.curve = ResponseCurve::Quadratic;

    auto out = map_once(cfg, 16384, 0);
    CHECK(std::abs(out.x - 8192) <= 2);
    CHECK(out.y == 0);
}

TEST_CASE("Stick inversion flips each axis independently", "[stick_mapper]") {
    StickConfig cfg;

    cfg.invert_x = true;
    auto out = map_once(cfg, 32767, 32767);
    CHECK(out.x < 0);
    CHECK(out.y > 0);
    CHECK(out.x == -out.y);

    cfg.invert_x = false;
    cfg.invert_y = true;
    out = map_once(cfg, 32767, 32767);
    CHECK(out.x > 0);
    CHECK(out.y < 0);
    CHECK(out.x == -out.y);
}

TEST_CASE("Keyboard and mouse modes keep the virtual stick centered", "[stick_mapper]") {
    auto mode = GENERATE(StickMode::Wasd, StickMode::Arrows, StickMode::Mouse);
    CAPTURE(mode);

    StickConfig cfg;
    cfg.mode = mode;

    auto out = map_once(cfg, 32767, -32768);
    CHECK(out.x == 0);
    CHECK(out.y == 0);
}

TEST_CASE("Reconfiguring the mapper takes effect on the next frame", "[stick_mapper]") {
    StickMapper mapper;
    SyntheticInput synth;
    int16_t x = 0;
    int16_t y = 0;

    StickConfig cfg;
    cfg.mode = StickMode::Mouse;
    mapper.apply_config(cfg);
    mapper.map(32767, 0, x, y, synth);
    CHECK(x == 0);

    cfg.mode = StickMode::Stick;
    mapper.apply_config(cfg);
    mapper.map(32767, 0, x, y, synth);
    CHECK(x == 32767);
}
