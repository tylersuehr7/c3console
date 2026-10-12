#include "platform/display_backend.hpp"
#include "platform/panel_rotation.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <numbers>
#include <optional>

using c3::platform::frame_transform;
using c3::platform::FrameTransform;
using c3::platform::Point2;
using c3::platform::rotated_size;
using c3::platform::Rotation;
using c3::platform::rotation_from_degrees;
using c3::platform::Size;
using c3::platform::to_degrees;
using c3::platform::to_logical;
using c3::platform::to_native;
using Catch::Matchers::WithinAbs;

namespace {

constexpr std::array<Rotation, 4> kAllRotations{Rotation::R0, Rotation::R90, Rotation::R180, Rotation::R270};

/// The spec's panel: scans out landscape, mounted portrait.
constexpr Size kNativeLandscape{.width = 1280, .height = 800};
constexpr Size kTargetLandscape{.width = 2388, .height = 1668};

[[nodiscard]] constexpr bool same(Point2 a, Point2 b) noexcept {
  return a.x == b.x && a.y == b.y;
}

/// Applies a FrameTransform the way NanoVG applies nvgTranslate followed by nvgRotate: the
/// point is rotated first, then translated. Rotation is clockwise-positive in y-down space.
[[nodiscard]] Point2 apply(const FrameTransform& t, Point2 p) noexcept {
  const float c = std::cos(t.angle_rad);
  const float s = std::sin(t.angle_rad);
  return {.x = p.x * c - p.y * s + t.translate_x, .y = p.x * s + p.y * c + t.translate_y};
}

// ---------------------------------------------------------------------------
// Degrees
// ---------------------------------------------------------------------------

TEST_CASE("rotation_from_degrees: accepts exactly the four quarter turns", "[panel_rotation]") {
  STATIC_CHECK(rotation_from_degrees(0) == Rotation::R0);
  STATIC_CHECK(rotation_from_degrees(90) == Rotation::R90);
  STATIC_CHECK(rotation_from_degrees(180) == Rotation::R180);
  STATIC_CHECK(rotation_from_degrees(270) == Rotation::R270);
}

TEST_CASE("rotation_from_degrees: rejects everything else, including aliases of valid turns", "[panel_rotation]") {
  for (const int deg : {-90, -1, 1, 45, 89, 91, 135, 269, 271, 360, 450, 900}) {
    INFO("degrees: " << deg);
    CHECK_FALSE(rotation_from_degrees(deg).has_value());
  }
}

TEST_CASE("to_degrees: round-trips every rotation", "[panel_rotation]") {
  for (const auto r : kAllRotations) {
    const int deg = to_degrees(r);
    CHECK(deg % 90 == 0);
    CHECK(deg >= 0);
    CHECK(deg < 360);
    REQUIRE(rotation_from_degrees(deg).has_value());
    CHECK(*rotation_from_degrees(deg) == r);
  }
  STATIC_CHECK(to_degrees(Rotation::R270) == 270);
}

// ---------------------------------------------------------------------------
// Sizes
// ---------------------------------------------------------------------------

TEST_CASE("rotated_size: quarter turns swap the axes, half and zero turns do not", "[panel_rotation]") {
  STATIC_CHECK(rotated_size(kNativeLandscape, Rotation::R0) == kNativeLandscape);
  STATIC_CHECK(rotated_size(kNativeLandscape, Rotation::R180) == kNativeLandscape);
  STATIC_CHECK(rotated_size(kNativeLandscape, Rotation::R90) == Size{.width = 800, .height = 1280});
  STATIC_CHECK(rotated_size(kNativeLandscape, Rotation::R270) == Size{.width = 800, .height = 1280});
}

TEST_CASE("rotated_size: turns the target's landscape scanout into the portrait panel", "[panel_rotation]") {
  STATIC_CHECK(rotated_size(kTargetLandscape, Rotation::R90) == Size{.width = 1668, .height = 2388});
}

TEST_CASE("rotated_size: applying the same quarter turn twice restores the native size", "[panel_rotation]") {
  for (const auto r : kAllRotations) {
    CHECK(rotated_size(rotated_size(kNativeLandscape, r), r) == kNativeLandscape);
  }
}

// ---------------------------------------------------------------------------
// to_native: hand-computed points for the 1280 x 800 landscape scanout
// ---------------------------------------------------------------------------

TEST_CASE("to_native: R0 is the identity", "[panel_rotation]") {
  STATIC_CHECK(same(to_native({.x = 0, .y = 0}, kNativeLandscape, Rotation::R0), {.x = 0, .y = 0}));
  STATIC_CHECK(same(to_native({.x = 100, .y = 200}, kNativeLandscape, Rotation::R0), {.x = 100, .y = 200}));
  STATIC_CHECK(
      same(to_native({.x = 1279.5F, .y = 799.5F}, kNativeLandscape, Rotation::R0), {.x = 1279.5F, .y = 799.5F}));
}

TEST_CASE("to_native: R90 lays the portrait panel's top edge down the native right edge", "[panel_rotation]") {
  // Logical panel is 800 wide by 1280 tall. Clockwise mounting: logical top-left sits at the
  // native top-right, logical +x runs down native +y, logical +y runs along native -x.
  STATIC_CHECK(same(to_native({.x = 0, .y = 0}, kNativeLandscape, Rotation::R90), {.x = 1280, .y = 0}));
  STATIC_CHECK(same(to_native({.x = 800, .y = 0}, kNativeLandscape, Rotation::R90), {.x = 1280, .y = 800}));
  STATIC_CHECK(same(to_native({.x = 0, .y = 1280}, kNativeLandscape, Rotation::R90), {.x = 0, .y = 0}));
  STATIC_CHECK(same(to_native({.x = 800, .y = 1280}, kNativeLandscape, Rotation::R90), {.x = 0, .y = 800}));
  STATIC_CHECK(same(to_native({.x = 100, .y = 200}, kNativeLandscape, Rotation::R90), {.x = 1080, .y = 100}));
}

TEST_CASE("to_native: R180 flips both axes", "[panel_rotation]") {
  STATIC_CHECK(same(to_native({.x = 0, .y = 0}, kNativeLandscape, Rotation::R180), {.x = 1280, .y = 800}));
  STATIC_CHECK(same(to_native({.x = 1280, .y = 800}, kNativeLandscape, Rotation::R180), {.x = 0, .y = 0}));
  STATIC_CHECK(same(to_native({.x = 100, .y = 200}, kNativeLandscape, Rotation::R180), {.x = 1180, .y = 600}));
}

TEST_CASE("to_native: R270 lays the portrait panel's top edge up the native left edge", "[panel_rotation]") {
  STATIC_CHECK(same(to_native({.x = 0, .y = 0}, kNativeLandscape, Rotation::R270), {.x = 0, .y = 800}));
  STATIC_CHECK(same(to_native({.x = 800, .y = 0}, kNativeLandscape, Rotation::R270), {.x = 0, .y = 0}));
  STATIC_CHECK(same(to_native({.x = 0, .y = 1280}, kNativeLandscape, Rotation::R270), {.x = 1280, .y = 800}));
  STATIC_CHECK(same(to_native({.x = 800, .y = 1280}, kNativeLandscape, Rotation::R270), {.x = 1280, .y = 0}));
  STATIC_CHECK(same(to_native({.x = 100, .y = 200}, kNativeLandscape, Rotation::R270), {.x = 200, .y = 700}));
}

TEST_CASE("to_native: the logical +x direction lands where each rotation says it should", "[panel_rotation]") {
  // Direction of one logical pixel step in +x, expressed in native pixels.
  const auto dir = [](Rotation r) {
    const Point2 o = to_native({.x = 0, .y = 0}, kNativeLandscape, r);
    const Point2 p = to_native({.x = 1, .y = 0}, kNativeLandscape, r);
    return Point2{.x = p.x - o.x, .y = p.y - o.y};
  };
  CHECK(same(dir(Rotation::R0), {.x = 1, .y = 0}));
  CHECK(same(dir(Rotation::R90), {.x = 0, .y = 1}));  // "logical +x runs down the native +y axis"
  CHECK(same(dir(Rotation::R180), {.x = -1, .y = 0}));
  CHECK(same(dir(Rotation::R270), {.x = 0, .y = -1}));
}

TEST_CASE("to_native: R90 and R270 are mirror images of each other", "[panel_rotation]") {
  // The same logical point under the two quarter turns lands at diagonally opposite spots.
  for (const Point2 p : {Point2{.x = 100, .y = 200}, Point2{.x = 0, .y = 0}, Point2{.x = 799, .y = 1279}}) {
    const Point2 a = to_native(p, kNativeLandscape, Rotation::R90);
    const Point2 b = to_native(p, kNativeLandscape, Rotation::R270);
    CHECK(a.x + b.x == 1280.0F);
    CHECK(a.y + b.y == 800.0F);
  }
}

// ---------------------------------------------------------------------------
// to_logical: the inverse, used by the touch source
// ---------------------------------------------------------------------------

TEST_CASE("to_logical: hand-computed native points come back as logical panel points", "[panel_rotation]") {
  STATIC_CHECK(same(to_logical({.x = 1080, .y = 100}, kNativeLandscape, Rotation::R90), {.x = 100, .y = 200}));
  STATIC_CHECK(same(to_logical({.x = 1180, .y = 600}, kNativeLandscape, Rotation::R180), {.x = 100, .y = 200}));
  STATIC_CHECK(same(to_logical({.x = 200, .y = 700}, kNativeLandscape, Rotation::R270), {.x = 100, .y = 200}));
  STATIC_CHECK(same(to_logical({.x = 100, .y = 200}, kNativeLandscape, Rotation::R0), {.x = 100, .y = 200}));
}

TEST_CASE("to_logical: undoes to_native exactly for every rotation", "[panel_rotation]") {
  // Drawing goes logical -> native, touch goes native -> logical. They must agree exactly, or a
  // tap on a drawn button would register beside it.
  constexpr std::array<Point2, 7> kPoints{{
      {.x = 0, .y = 0},
      {.x = 0.5F, .y = 0.5F},
      {.x = 100, .y = 200},
      {.x = 399.5F, .y = 640.25F},
      {.x = 799, .y = 1279},
      {.x = 799.5F, .y = 1279.5F},
      {.x = 800, .y = 1280},
  }};
  for (const auto r : kAllRotations) {
    for (const Point2 p : kPoints) {
      INFO("rotation " << to_degrees(r) << " point (" << p.x << ", " << p.y << ")");
      const Point2 back = to_logical(to_native(p, kNativeLandscape, r), kNativeLandscape, r);
      CHECK(back.x == p.x);
      CHECK(back.y == p.y);
    }
  }
}

TEST_CASE("to_native: undoes to_logical exactly for every rotation", "[panel_rotation]") {
  constexpr std::array<Point2, 5> kNativePoints{{
      {.x = 0, .y = 0},
      {.x = 0.5F, .y = 0.5F},
      {.x = 640, .y = 400},
      {.x = 1279.5F, .y = 799.5F},
      {.x = 1280, .y = 800},
  }};
  for (const auto r : kAllRotations) {
    for (const Point2 q : kNativePoints) {
      INFO("rotation " << to_degrees(r) << " native (" << q.x << ", " << q.y << ")");
      const Point2 back = to_native(to_logical(q, kNativeLandscape, r), kNativeLandscape, r);
      CHECK(back.x == q.x);
      CHECK(back.y == q.y);
    }
  }
}

TEST_CASE("to_logical: a touch anywhere on the native sensor lands inside the logical panel", "[panel_rotation]") {
  for (const auto r : kAllRotations) {
    const Size logical = rotated_size(kNativeLandscape, r);
    for (int ny = 0; ny < kNativeLandscape.height; ny += 199) {
      for (int nx = 0; nx < kNativeLandscape.width; nx += 127) {
        // Touch controllers report pixel centres.
        const Point2 q{.x = static_cast<float>(nx) + 0.5F, .y = static_cast<float>(ny) + 0.5F};
        const Point2 p = to_logical(q, kNativeLandscape, r);
        INFO("rotation " << to_degrees(r) << " native (" << q.x << ", " << q.y << ") -> (" << p.x << ", " << p.y
                         << ")");
        CHECK(p.x >= 0.0F);
        CHECK(p.y >= 0.0F);
        CHECK(p.x < static_cast<float>(logical.width));
        CHECK(p.y < static_cast<float>(logical.height));
      }
    }
  }
}

TEST_CASE("to_native: pixel centres map to pixel centres inside the native grid", "[panel_rotation]") {
  for (const auto r : kAllRotations) {
    const Size logical = rotated_size(kNativeLandscape, r);
    const Point2 first{.x = 0.5F, .y = 0.5F};
    const Point2 last{.x = static_cast<float>(logical.width) - 0.5F, .y = static_cast<float>(logical.height) - 0.5F};
    for (const Point2 p : {first, last}) {
      const Point2 q = to_native(p, kNativeLandscape, r);
      INFO("rotation " << to_degrees(r));
      CHECK(q.x >= 0.0F);
      CHECK(q.y >= 0.0F);
      CHECK(q.x < static_cast<float>(kNativeLandscape.width));
      CHECK(q.y < static_cast<float>(kNativeLandscape.height));
      CHECK(std::fmod(q.x, 1.0F) == 0.5F);
      CHECK(std::fmod(q.y, 1.0F) == 0.5F);
    }
  }
}

// ---------------------------------------------------------------------------
// frame_transform: what the renderer applies must equal what the touch path inverts
// ---------------------------------------------------------------------------

TEST_CASE("frame_transform: R0 is the identity transform", "[panel_rotation]") {
  constexpr FrameTransform t = frame_transform(kNativeLandscape, Rotation::R0);
  STATIC_CHECK(t.translate_x == 0.0F);
  STATIC_CHECK(t.translate_y == 0.0F);
  STATIC_CHECK(t.angle_rad == 0.0F);
}

TEST_CASE("frame_transform: translations put the logical origin at the right native corner", "[panel_rotation]") {
  constexpr float kHalfPi = std::numbers::pi_v<float> / 2.0F;
  constexpr FrameTransform r90 = frame_transform(kNativeLandscape, Rotation::R90);
  STATIC_CHECK(r90.translate_x == 1280.0F);
  STATIC_CHECK(r90.translate_y == 0.0F);
  STATIC_CHECK(r90.angle_rad == kHalfPi);

  constexpr FrameTransform r180 = frame_transform(kNativeLandscape, Rotation::R180);
  STATIC_CHECK(r180.translate_x == 1280.0F);
  STATIC_CHECK(r180.translate_y == 800.0F);
  STATIC_CHECK(r180.angle_rad == 2.0F * kHalfPi);

  constexpr FrameTransform r270 = frame_transform(kNativeLandscape, Rotation::R270);
  STATIC_CHECK(r270.translate_x == 0.0F);
  STATIC_CHECK(r270.translate_y == 800.0F);
  STATIC_CHECK(r270.angle_rad == -kHalfPi);
}

TEST_CASE("frame_transform: applied as NanoVG applies it, it reproduces to_native for every rotation",
          "[panel_rotation]") {
  // This is the guarantee the header promises: drawing and touch agree by construction.
  constexpr std::array<Point2, 6> kPoints{{
      {.x = 0, .y = 0},
      {.x = 0.5F, .y = 0.5F},
      {.x = 100, .y = 200},
      {.x = 400, .y = 640},
      {.x = 799.5F, .y = 1279.5F},
      {.x = 800, .y = 1280},
  }};
  for (const auto r : kAllRotations) {
    const FrameTransform t = frame_transform(kNativeLandscape, r);
    for (const Point2 p : kPoints) {
      const Point2 drawn = apply(t, p);
      const Point2 expected = to_native(p, kNativeLandscape, r);
      INFO("rotation " << to_degrees(r) << " point (" << p.x << ", " << p.y << ")");
      CHECK_THAT(drawn.x, WithinAbs(expected.x, 1e-3));
      CHECK_THAT(drawn.y, WithinAbs(expected.y, 1e-3));
    }
  }
}

TEST_CASE("frame_transform: a drawn point can be touched back at the same logical place", "[panel_rotation]") {
  // Draw a button at logical (300, 900), find where it landed natively, convert the touch back.
  for (const auto r : kAllRotations) {
    const Point2 button{.x = 300, .y = 900};
    const Point2 on_glass = apply(frame_transform(kNativeLandscape, r), button);
    const Point2 touched = to_logical(on_glass, kNativeLandscape, r);
    INFO("rotation " << to_degrees(r));
    CHECK_THAT(touched.x, WithinAbs(button.x, 1e-3));
    CHECK_THAT(touched.y, WithinAbs(button.y, 1e-3));
  }
}

TEST_CASE("frame_transform: the same identities hold for the target panel geometry", "[panel_rotation]") {
  for (const auto r : kAllRotations) {
    const FrameTransform t = frame_transform(kTargetLandscape, r);
    const Size logical = rotated_size(kTargetLandscape, r);
    const Point2 centre{.x = static_cast<float>(logical.width) / 2, .y = static_cast<float>(logical.height) / 2};
    const Point2 drawn = apply(t, centre);
    const Point2 expected = to_native(centre, kTargetLandscape, r);
    CHECK_THAT(drawn.x, WithinAbs(expected.x, 1e-2));
    CHECK_THAT(drawn.y, WithinAbs(expected.y, 1e-2));
    CHECK_THAT(drawn.x, WithinAbs(2388.0 / 2, 1e-2));  // the panel centre is the glass centre under any turn
    CHECK_THAT(drawn.y, WithinAbs(1668.0 / 2, 1e-2));
  }
}

}  // namespace
