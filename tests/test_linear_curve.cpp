#include "utils/linear_curve.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdint>
#include <limits>

using c3::utils::CurvePoint;
using c3::utils::LinearCurve;
using Catch::Matchers::WithinAbs;

namespace {

// Numbers in this file are shapes chosen to make the arithmetic obvious. They are not
// sender calibration data; that lives in config/calibration.json and comes from the bench.

[[nodiscard]] LinearCurve::Points make_points(std::initializer_list<CurvePoint> list) noexcept {
  LinearCurve::Points points;
  for (const auto& p : list) {
    static_cast<void>(points.push_back(p));
  }
  return points;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST_CASE("LinearCurve: default curve is empty", "[linear_curve]") {
  const LinearCurve curve;
  CHECK(curve.size() == 0);
  CHECK(curve.points().empty());
}

TEST_CASE("LinearCurve: two-point constructor stores both points in order", "[linear_curve]") {
  const LinearCurve curve(CurvePoint{.raw = 0, .value = 0.0F}, CurvePoint{.raw = 90, .value = 100.0F});
  REQUIRE(curve.size() == 2);
  CHECK(curve.points()[0].raw == 0);
  CHECK(curve.points()[0].value == 0.0F);
  CHECK(curve.points()[1].raw == 90);
  CHECK(curve.points()[1].value == 100.0F);
}

TEST_CASE("LinearCurve: two-point constructor and interpolate are usable in constant expressions", "[linear_curve]") {
  constexpr LinearCurve curve(CurvePoint{.raw = 0, .value = 0.0F}, CurvePoint{.raw = 90, .value = 100.0F});
  STATIC_CHECK(curve.size() == 2);
  STATIC_CHECK(curve.interpolate(45) == 50.0F);
  STATIC_CHECK(curve.covers(45));
  STATIC_CHECK_FALSE(curve.covers(91));
}

TEST_CASE("LinearCurve: kCapacity matches the Points container", "[linear_curve]") {
  STATIC_CHECK(LinearCurve::kCapacity == 24);
  STATIC_CHECK(LinearCurve::Points::kCapacity == LinearCurve::kCapacity);
}

// ---------------------------------------------------------------------------
// interpolate: inside the table
// ---------------------------------------------------------------------------

TEST_CASE("interpolate: empty curve returns 0", "[linear_curve]") {
  const LinearCurve curve;
  CHECK(curve.interpolate(0) == 0.0F);
  CHECK(curve.interpolate(12345) == 0.0F);
  CHECK(curve.interpolate(-12345) == 0.0F);
}

TEST_CASE("interpolate: single-point curve returns 0 because no segment exists", "[linear_curve]") {
  LinearCurve curve;
  REQUIRE(curve.add(CurvePoint{.raw = 10, .value = 55.0F}));
  CHECK(curve.interpolate(10) == 0.0F);
}

TEST_CASE("interpolate: returns the knot values exactly at the knots", "[linear_curve]") {
  const LinearCurve curve(CurvePoint{.raw = 0, .value = 0.0F}, CurvePoint{.raw = 90, .value = 100.0F});
  CHECK(curve.interpolate(0) == 0.0F);
  CHECK(curve.interpolate(90) == 100.0F);
}

TEST_CASE("interpolate: midpoint of a two-point curve is the midpoint value", "[linear_curve]") {
  const LinearCurve curve(CurvePoint{.raw = 0, .value = 0.0F}, CurvePoint{.raw = 90, .value = 100.0F});
  CHECK_THAT(curve.interpolate(45), WithinAbs(50.0, 1e-4));
  CHECK_THAT(curve.interpolate(9), WithinAbs(10.0, 1e-4));
  CHECK_THAT(curve.interpolate(81), WithinAbs(90.0, 1e-4));
}

TEST_CASE("interpolate: selects the correct segment of a piecewise curve", "[linear_curve]") {
  // Slope 10 on the first segment, slope 1 on the second.
  const auto curve = LinearCurve::from_points(make_points({
      CurvePoint{.raw = 0, .value = 0.0F},
      CurvePoint{.raw = 10, .value = 100.0F},
      CurvePoint{.raw = 20, .value = 110.0F},
  }));
  REQUIRE(curve.has_value());
  CHECK_THAT(curve->interpolate(5), WithinAbs(50.0, 1e-4));
  CHECK_THAT(curve->interpolate(15), WithinAbs(105.0, 1e-4));
}

TEST_CASE("interpolate: interior knots return their own value and belong to the following segment", "[linear_curve]") {
  const auto curve = LinearCurve::from_points(make_points({
      CurvePoint{.raw = 0, .value = 0.0F},
      CurvePoint{.raw = 10, .value = 100.0F},
      CurvePoint{.raw = 20, .value = 110.0F},
  }));
  REQUIRE(curve.has_value());
  CHECK_THAT(curve->interpolate(10), WithinAbs(100.0, 1e-4));
  CHECK_THAT(curve->interpolate(11), WithinAbs(101.0, 1e-4));
}

TEST_CASE("interpolate: decreasing values interpolate correctly (NTC-shaped curve)", "[linear_curve]") {
  // Resistance rises as temperature falls; value is monotonic decreasing in raw.
  const LinearCurve curve(CurvePoint{.raw = 55, .value = 250.0F}, CurvePoint{.raw = 1365, .value = 100.0F});
  CHECK_THAT(curve.interpolate(710), WithinAbs(175.0, 1e-4));
  CHECK_THAT(curve.interpolate(55), WithinAbs(250.0, 1e-4));
  CHECK_THAT(curve.interpolate(1365), WithinAbs(100.0, 1e-4));
}

TEST_CASE("interpolate: values need not be monotonic", "[linear_curve]") {
  const auto curve = LinearCurve::from_points(make_points({
      CurvePoint{.raw = 0, .value = 10.0F},
      CurvePoint{.raw = 10, .value = 0.0F},
      CurvePoint{.raw = 20, .value = 10.0F},
  }));
  REQUIRE(curve.has_value());
  CHECK_THAT(curve->interpolate(5), WithinAbs(5.0, 1e-4));
  CHECK_THAT(curve->interpolate(15), WithinAbs(5.0, 1e-4));
}

TEST_CASE("interpolate: negative raw values are handled", "[linear_curve]") {
  const LinearCurve curve(CurvePoint{.raw = -100, .value = -1.0F}, CurvePoint{.raw = 100, .value = 1.0F});
  CHECK_THAT(curve.interpolate(0), WithinAbs(0.0, 1e-6));
  CHECK_THAT(curve.interpolate(-50), WithinAbs(-0.5, 1e-6));
  CHECK_THAT(curve.interpolate(50), WithinAbs(0.5, 1e-6));
}

TEST_CASE("interpolate: full int32 span does not overflow", "[linear_curve]") {
  constexpr auto kMin = std::numeric_limits<std::int32_t>::min();
  constexpr auto kMax = std::numeric_limits<std::int32_t>::max();
  const LinearCurve curve(CurvePoint{.raw = kMin, .value = -1.0F}, CurvePoint{.raw = kMax, .value = 1.0F});
  CHECK_THAT(curve.interpolate(0), WithinAbs(0.0, 1e-6));
  CHECK_THAT(curve.interpolate(kMin), WithinAbs(-1.0, 1e-6));
  CHECK_THAT(curve.interpolate(kMax), WithinAbs(1.0, 1e-6));
  CHECK(std::isfinite(curve.interpolate(kMin / 2)));
}

// ---------------------------------------------------------------------------
// interpolate: extrapolation beyond the table
// ---------------------------------------------------------------------------

TEST_CASE("interpolate: extrapolates below the first point along the first segment", "[linear_curve]") {
  const LinearCurve curve(CurvePoint{.raw = 0, .value = 0.0F}, CurvePoint{.raw = 90, .value = 100.0F});
  CHECK_THAT(curve.interpolate(-45), WithinAbs(-50.0, 1e-4));
  CHECK_THAT(curve.interpolate(-90), WithinAbs(-100.0, 1e-4));
}

TEST_CASE("interpolate: extrapolates above the last point along the last segment", "[linear_curve]") {
  const LinearCurve curve(CurvePoint{.raw = 0, .value = 0.0F}, CurvePoint{.raw = 90, .value = 100.0F});
  CHECK_THAT(curve.interpolate(135), WithinAbs(150.0, 1e-4));
  CHECK_THAT(curve.interpolate(180), WithinAbs(200.0, 1e-4));
}

TEST_CASE("interpolate: extrapolation on a piecewise curve uses the end segments only", "[linear_curve]") {
  // Slope 10 on the first segment, slope 1 on the second.
  const auto curve = LinearCurve::from_points(make_points({
      CurvePoint{.raw = 0, .value = 0.0F},
      CurvePoint{.raw = 10, .value = 100.0F},
      CurvePoint{.raw = 20, .value = 110.0F},
  }));
  REQUIRE(curve.has_value());
  CHECK_THAT(curve->interpolate(-10), WithinAbs(-100.0, 1e-4));
  CHECK_THAT(curve->interpolate(30), WithinAbs(120.0, 1e-4));
}

// ---------------------------------------------------------------------------
// covers
// ---------------------------------------------------------------------------

TEST_CASE("covers: empty curve covers nothing", "[linear_curve]") {
  const LinearCurve curve;
  CHECK_FALSE(curve.covers(0));
  CHECK_FALSE(curve.covers(-1));
  CHECK_FALSE(curve.covers(1));
}

TEST_CASE("covers: true inside and on the boundaries, false outside", "[linear_curve]") {
  const LinearCurve curve(CurvePoint{.raw = 10, .value = 0.0F}, CurvePoint{.raw = 20, .value = 1.0F});
  CHECK(curve.covers(10));
  CHECK(curve.covers(15));
  CHECK(curve.covers(20));
  CHECK_FALSE(curve.covers(9));
  CHECK_FALSE(curve.covers(21));
}

TEST_CASE("covers: single-point curve covers exactly that raw value", "[linear_curve]") {
  LinearCurve curve;
  REQUIRE(curve.add(CurvePoint{.raw = 7, .value = 1.0F}));
  CHECK(curve.covers(7));
  CHECK_FALSE(curve.covers(6));
  CHECK_FALSE(curve.covers(8));
}

// ---------------------------------------------------------------------------
// from_points
// ---------------------------------------------------------------------------

TEST_CASE("from_points: rejects fewer than two points", "[linear_curve]") {
  CHECK_FALSE(LinearCurve::from_points(make_points({})).has_value());
  CHECK_FALSE(LinearCurve::from_points(make_points({CurvePoint{.raw = 0, .value = 0.0F}})).has_value());
}

TEST_CASE("from_points: accepts exactly two strictly increasing points", "[linear_curve]") {
  const auto curve = LinearCurve::from_points(make_points({
      CurvePoint{.raw = 1, .value = 0.0F},
      CurvePoint{.raw = 90, .value = 80.0F},
  }));
  REQUIRE(curve.has_value());
  CHECK(curve->size() == 2);
  CHECK(curve->points()[0].raw == 1);
  CHECK(curve->points()[1].raw == 90);
}

TEST_CASE("from_points: preserves every point of a valid table", "[linear_curve]") {
  const auto input = make_points({
      CurvePoint{.raw = -5, .value = 1.0F},
      CurvePoint{.raw = 0, .value = 2.0F},
      CurvePoint{.raw = 3, .value = 3.0F},
      CurvePoint{.raw = 100, .value = 4.0F},
  });
  const auto curve = LinearCurve::from_points(input);
  REQUIRE(curve.has_value());
  REQUIRE(curve->size() == input.size());
  for (std::size_t i = 0; i < input.size(); ++i) {
    CHECK(curve->points()[i].raw == input[i].raw);
    CHECK(curve->points()[i].value == input[i].value);
  }
}

TEST_CASE("from_points: accepts a table at full capacity", "[linear_curve]") {
  LinearCurve::Points points;
  for (std::int32_t i = 0; i < static_cast<std::int32_t>(LinearCurve::kCapacity); ++i) {
    REQUIRE(points.push_back(CurvePoint{.raw = i * 10, .value = static_cast<float>(i)}));
  }
  REQUIRE(points.full());
  const auto curve = LinearCurve::from_points(points);
  REQUIRE(curve.has_value());
  CHECK(curve->size() == LinearCurve::kCapacity);
  CHECK_THAT(curve->interpolate(115), WithinAbs(11.5, 1e-4));
}

TEST_CASE("from_points: rejects duplicate raw values", "[linear_curve]") {
  const auto curve = LinearCurve::from_points(make_points({
      CurvePoint{.raw = 0, .value = 0.0F},
      CurvePoint{.raw = 10, .value = 1.0F},
      CurvePoint{.raw = 10, .value = 2.0F},
  }));
  CHECK_FALSE(curve.has_value());
}

TEST_CASE("from_points: rejects raw values that are not strictly increasing", "[linear_curve]") {
  const auto curve = LinearCurve::from_points(make_points({
      CurvePoint{.raw = 0, .value = 0.0F},
      CurvePoint{.raw = 20, .value = 1.0F},
      CurvePoint{.raw = 10, .value = 2.0F},
  }));
  CHECK_FALSE(curve.has_value());
}

TEST_CASE("from_points: rejects NaN values", "[linear_curve]") {
  const auto curve = LinearCurve::from_points(make_points({
      CurvePoint{.raw = 0, .value = 0.0F},
      CurvePoint{.raw = 10, .value = std::numeric_limits<float>::quiet_NaN()},
  }));
  CHECK_FALSE(curve.has_value());
}

TEST_CASE("from_points: rejects infinite values", "[linear_curve]") {
  const auto positive = LinearCurve::from_points(make_points({
      CurvePoint{.raw = 0, .value = std::numeric_limits<float>::infinity()},
      CurvePoint{.raw = 10, .value = 1.0F},
  }));
  const auto negative = LinearCurve::from_points(make_points({
      CurvePoint{.raw = 0, .value = 0.0F},
      CurvePoint{.raw = 10, .value = -std::numeric_limits<float>::infinity()},
  }));
  CHECK_FALSE(positive.has_value());
  CHECK_FALSE(negative.has_value());
}

// ---------------------------------------------------------------------------
// add
// ---------------------------------------------------------------------------

TEST_CASE("add: two adds to an empty curve produce a working two-point curve", "[linear_curve]") {
  LinearCurve curve;
  CHECK(curve.add(CurvePoint{.raw = 0, .value = 0.0F}));
  CHECK(curve.add(CurvePoint{.raw = 90, .value = 100.0F}));
  CHECK(curve.size() == 2);
  CHECK_THAT(curve.interpolate(45), WithinAbs(50.0, 1e-4));
}

TEST_CASE("add: keeps points sorted by raw regardless of insertion order", "[linear_curve]") {
  LinearCurve curve;
  CHECK(curve.add(CurvePoint{.raw = 90, .value = 100.0F}));
  CHECK(curve.add(CurvePoint{.raw = 0, .value = 0.0F}));
  CHECK(curve.add(CurvePoint{.raw = 45, .value = 60.0F}));
  REQUIRE(curve.size() == 3);
  CHECK(curve.points()[0].raw == 0);
  CHECK(curve.points()[1].raw == 45);
  CHECK(curve.points()[2].raw == 90);
  CHECK(curve.points()[1].value == 60.0F);
  CHECK_THAT(curve.interpolate(22), WithinAbs(29.3333, 1e-3));
}

TEST_CASE("add: a point with an existing raw replaces the value without growing", "[linear_curve]") {
  LinearCurve curve(CurvePoint{.raw = 0, .value = 0.0F}, CurvePoint{.raw = 90, .value = 100.0F});
  CHECK(curve.add(CurvePoint{.raw = 90, .value = 80.0F}));
  REQUIRE(curve.size() == 2);
  CHECK(curve.points()[1].raw == 90);
  CHECK(curve.points()[1].value == 80.0F);
  CHECK_THAT(curve.interpolate(45), WithinAbs(40.0, 1e-4));
}

TEST_CASE("add: rejects NaN and leaves the curve unchanged", "[linear_curve]") {
  LinearCurve curve(CurvePoint{.raw = 0, .value = 0.0F}, CurvePoint{.raw = 90, .value = 100.0F});
  CHECK_FALSE(curve.add(CurvePoint{.raw = 45, .value = std::numeric_limits<float>::quiet_NaN()}));
  CHECK(curve.size() == 2);
  CHECK_THAT(curve.interpolate(45), WithinAbs(50.0, 1e-4));
}

TEST_CASE("add: rejects infinity even when it would replace an existing point", "[linear_curve]") {
  LinearCurve curve(CurvePoint{.raw = 0, .value = 0.0F}, CurvePoint{.raw = 90, .value = 100.0F});
  CHECK_FALSE(curve.add(CurvePoint{.raw = 90, .value = std::numeric_limits<float>::infinity()}));
  CHECK(curve.points()[1].value == 100.0F);
}

TEST_CASE("add: returns false when the table is full", "[linear_curve]") {
  LinearCurve curve;
  for (std::int32_t i = 0; i < static_cast<std::int32_t>(LinearCurve::kCapacity); ++i) {
    REQUIRE(curve.add(CurvePoint{.raw = i, .value = static_cast<float>(i)}));
  }
  REQUIRE(curve.size() == LinearCurve::kCapacity);
  CHECK_FALSE(curve.add(CurvePoint{.raw = 1000, .value = 1.0F}));
  CHECK(curve.size() == LinearCurve::kCapacity);
  CHECK(curve.points().back().raw == static_cast<std::int32_t>(LinearCurve::kCapacity) - 1);
}

TEST_CASE("add: replacing an existing raw still succeeds when the table is full", "[linear_curve]") {
  LinearCurve curve;
  for (std::int32_t i = 0; i < static_cast<std::int32_t>(LinearCurve::kCapacity); ++i) {
    REQUIRE(curve.add(CurvePoint{.raw = i, .value = static_cast<float>(i)}));
  }
  CHECK(curve.add(CurvePoint{.raw = 5, .value = 500.0F}));
  CHECK(curve.size() == LinearCurve::kCapacity);
  CHECK(curve.points()[5].value == 500.0F);
}

TEST_CASE("add: a sorted table built by add agrees with from_points", "[linear_curve]") {
  LinearCurve added;
  CHECK(added.add(CurvePoint{.raw = 20, .value = 110.0F}));
  CHECK(added.add(CurvePoint{.raw = 0, .value = 0.0F}));
  CHECK(added.add(CurvePoint{.raw = 10, .value = 100.0F}));

  const auto built = LinearCurve::from_points(make_points({
      CurvePoint{.raw = 0, .value = 0.0F},
      CurvePoint{.raw = 10, .value = 100.0F},
      CurvePoint{.raw = 20, .value = 110.0F},
  }));
  REQUIRE(built.has_value());

  for (const std::int32_t raw : {-10, 0, 5, 10, 15, 20, 30}) {
    CHECK(added.interpolate(raw) == built->interpolate(raw));
  }
}

}  // namespace
