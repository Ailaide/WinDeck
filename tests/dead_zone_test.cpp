#include "mapper/dead_zone.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdlib>

using windeck::config::ResponseCurve;
using windeck::mapper::apply_curve;
using windeck::mapper::apply_stick_deadzone;
using windeck::mapper::apply_trigger_deadzone;
using Catch::Matchers::WithinAbs;

namespace {

struct StickOut {
    int16_t x = 0;
    int16_t y = 0;
};

StickOut dz(int16_t raw_x, int16_t raw_y, float inner, float outer,
            ResponseCurve curve = ResponseCurve::Linear) {
    StickOut out;
    apply_stick_deadzone(raw_x, raw_y, out.x, out.y, inner, outer, curve);
    return out;
}

} // namespace

TEST_CASE("Response curves map [0, 1] onto [0, 1]", "[dead_zone]") {
    auto curve = GENERATE(ResponseCurve::Linear, ResponseCurve::Quadratic,
                          ResponseCurve::InverseQuadratic);
    CAPTURE(curve);

    CHECK(apply_curve(0.0f, curve) == 0.0f);
    CHECK(apply_curve(1.0f, curve) == 1.0f);

    float prev = 0.0f;
    for (int i = 1; i <= 100; ++i) {
        float v = apply_curve(static_cast<float>(i) / 100.0f, curve);
        CHECK(v >= prev);
        prev = v;
    }
}

TEST_CASE("Response curve shapes", "[dead_zone]") {
    CHECK_THAT(apply_curve(0.5f, ResponseCurve::Linear), WithinAbs(0.5, 1e-6));
    CHECK_THAT(apply_curve(0.5f, ResponseCurve::Quadratic), WithinAbs(0.25, 1e-6));
    CHECK_THAT(apply_curve(0.5f, ResponseCurve::InverseQuadratic), WithinAbs(0.75, 1e-6));
}

TEST_CASE("Stick inside the inner dead zone is centered", "[dead_zone]") {
    auto out = dz(0, 0, 0.05f, 0.95f);
    CHECK(out.x == 0);
    CHECK(out.y == 0);

    out = dz(1000, -1000, 0.05f, 0.95f);  // magnitude ~0.043
    CHECK(out.x == 0);
    CHECK(out.y == 0);
}

TEST_CASE("Stick dead zone is radial, not per-axis", "[dead_zone]") {
    // Each axis alone (0.04) is inside the 0.05 dead zone, but the combined
    // magnitude (~0.057) is outside it, so the stick must move.
    auto out = dz(1311, 1311, 0.05f, 0.95f);
    CHECK(out.x > 0);
    CHECK(out.y > 0);
    CHECK(out.x == out.y);
}

TEST_CASE("Stick at or beyond the outer dead zone saturates", "[dead_zone]") {
    auto out = dz(32767, 0, 0.05f, 0.95f);
    CHECK(out.x == 32767);
    CHECK(out.y == 0);

    // Past the outer edge the direction is renormalized in float, so allow
    // one unit of rounding.
    out = dz(0, 31200, 0.05f, 0.95f);  // just past the 0.95 outer edge
    CHECK(out.x == 0);
    CHECK(out.y >= 32766);

    // The raw minimum is slightly below -1.0: must not overflow.
    out = dz(-32768, 0, 0.05f, 0.95f);
    CHECK(out.x <= -32766);
    CHECK(out.y == 0);
}

TEST_CASE("Full diagonal deflection stays on the unit circle", "[dead_zone]") {
    auto out = dz(32767, 32767, 0.05f, 0.95f);
    CHECK(out.x == out.y);
    double magnitude = std::hypot(static_cast<double>(out.x), static_cast<double>(out.y));
    CHECK_THAT(magnitude, WithinAbs(32767.0, 2.0));
}

TEST_CASE("Stick output is symmetric around the center", "[dead_zone]") {
    auto curve = GENERATE(ResponseCurve::Linear, ResponseCurve::Quadratic,
                          ResponseCurve::InverseQuadratic);
    auto raw = GENERATE(as<int16_t>{}, 3000, 12000, 20000, 32767);
    CAPTURE(curve, raw);

    auto pos = dz(raw, 0, 0.05f, 0.95f, curve);
    auto neg = dz(static_cast<int16_t>(-raw), 0, 0.05f, 0.95f, curve);
    CHECK(neg.x == -pos.x);
}

TEST_CASE("Stick output rises monotonically with deflection", "[dead_zone]") {
    auto curve = GENERATE(ResponseCurve::Linear, ResponseCurve::Quadratic,
                          ResponseCurve::InverseQuadratic);
    CAPTURE(curve);

    // Up to the outer edge (0.9); beyond it the output is saturated.
    int16_t prev = 0;
    for (int raw = 0; raw < 29400; raw += 256) {
        auto out = dz(static_cast<int16_t>(raw), 0, 0.1f, 0.9f, curve);
        CHECK(out.x >= prev);
        prev = out.x;
    }
}

TEST_CASE("Curve is applied to the rescaled range between the dead zones", "[dead_zone]") {
    // inner 0.1, outer 0.9: a raw half deflection is the middle of that range.
    const int16_t half = 16384;

    auto linear = dz(half, 0, 0.1f, 0.9f, ResponseCurve::Linear);
    CHECK(std::abs(linear.x - 16384) <= 2);

    auto quadratic = dz(half, 0, 0.1f, 0.9f, ResponseCurve::Quadratic);
    CHECK(std::abs(quadratic.x - 8192) <= 2);

    auto inverse = dz(half, 0, 0.1f, 0.9f, ResponseCurve::InverseQuadratic);
    CHECK(std::abs(inverse.x - 24575) <= 2);
}

TEST_CASE("Empty dead zone range yields a centered stick", "[dead_zone]") {
    auto out = dz(32767, 32767, 0.5f, 0.5f);
    CHECK(out.x == 0);
    CHECK(out.y == 0);
}

TEST_CASE("Trigger threshold and 8-bit scaling", "[dead_zone]") {
    SECTION("No threshold scales linearly") {
        CHECK(apply_trigger_deadzone(0, 0.0f) == 0);
        CHECK(apply_trigger_deadzone(16384, 0.0f) == 127);
        CHECK(apply_trigger_deadzone(32767, 0.0f) == 255);
    }
    SECTION("Below the threshold reads as released") {
        CHECK(apply_trigger_deadzone(600, 0.02f) == 0);
    }
    SECTION("Range above the threshold is rescaled to 0-255") {
        CHECK(apply_trigger_deadzone(16384, 0.5f) == 0);
        CHECK(apply_trigger_deadzone(24575, 0.5f) == 127);
        CHECK(apply_trigger_deadzone(32767, 0.5f) == 255);
    }
    SECTION("Threshold of 1.0 acts as a digital trigger") {
        CHECK(apply_trigger_deadzone(32766, 1.0f) == 0);
        CHECK(apply_trigger_deadzone(32767, 1.0f) == 255);
    }
    SECTION("Out-of-range raw values clamp to 255") {
        CHECK(apply_trigger_deadzone(65535, 0.02f) == 255);
    }
}
