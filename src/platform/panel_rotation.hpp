// Maps the logical portrait panel onto a landscape framebuffer, and touches back again.
//
// The LCD is mounted portrait, but most panels scan out landscape: the native framebuffer
// is, say, 1280 wide by 800 tall while the UI is designed for 800 by 1280. The display
// backend draws the logical portrait panel into the native framebuffer through this
// transform, and the touch source runs the same transform backwards, so a tap on a drawn
// button lands on that button by construction rather than by two separately maintained
// pieces of arithmetic. Scene code never sees the rotation at all.
//
// Conventions:
// - "Native" is the framebuffer the hardware scans out and the touch controller reports in.
//   "Logical" is the portrait panel the scenes draw in, whose size is rotated_size().
// - Rotation is the panel's mounting rotation, clockwise, as seen by the driver sitting in
//   front of it. R90 means the glass was turned a quarter turn clockwise, so the logical
//   top-left corner sits at the native top-right and logical +x runs down native +y.
// - Points are continuous pixel coordinates with the origin at the top-left corner and the
//   far edges at exactly width and height. Pixel centres are at .5, and a pixel centre always
//   maps to a pixel centre. Edges map to edges, so a corner can come back as exactly width.
//
// Everything here is constexpr, allocation free and exception free. Pure; tested.
#pragma once

#include "display_backend.hpp"

#include <cstdint>
#include <numbers>
#include <optional>

namespace c3::platform {

/// Mounting rotation of the panel, clockwise as the driver sees it.
enum class Rotation : std::uint8_t {
  R0,
  R90,  // logical +x runs down the native +y axis
  R180,
  R270
};

/// Parses the `[display] rotation` config value.
///
/// @param deg the configured angle
/// @return the rotation for exactly 0, 90, 180 or 270; otherwise nullopt (no wrapping, no
///         nearest-quarter rounding: a typo in the config is an error, not a guess)
[[nodiscard]] constexpr std::optional<Rotation> rotation_from_degrees(int deg) noexcept {
  switch (deg) {
    case 0:
      return Rotation::R0;
    case 90:
      return Rotation::R90;
    case 180:
      return Rotation::R180;
    case 270:
      return Rotation::R270;
    default:
      return std::nullopt;
  }
}

/// @return the rotation in degrees: 0, 90, 180 or 270
[[nodiscard]] constexpr int to_degrees(Rotation r) noexcept {
  return static_cast<int>(r) * 90;
}

/// Logical panel size for a native mode under `r`.
///
/// Quarter turns swap width and height; a half turn or no turn keeps them.
///
/// @param native the framebuffer size the hardware scans out
/// @param r the mounting rotation
/// @return the size scene code should lay out against
[[nodiscard]] constexpr Size rotated_size(Size native, Rotation r) noexcept {
  return (r == Rotation::R90 || r == Rotation::R270) ? Size{.width = native.height, .height = native.width} : native;
}

/// A point in continuous pixel coordinates; which space it is in depends on the function.
struct Point2 {
  float x = 0.0F;
  float y = 0.0F;
};

/// A logical panel point to native framebuffer pixels: the drawing direction.
///
/// @param logical a point in the rotated_size() panel
/// @param native the framebuffer size
/// @param r the mounting rotation
/// @return the same place on the glass in native pixels
[[nodiscard]] constexpr Point2 to_native(Point2 logical, Size native, Rotation r) noexcept {
  const auto w = static_cast<float>(native.width);
  const auto h = static_cast<float>(native.height);
  switch (r) {
    case Rotation::R0:
      return logical;
    case Rotation::R90:
      return {.x = w - logical.y, .y = logical.x};
    case Rotation::R180:
      return {.x = w - logical.x, .y = h - logical.y};
    case Rotation::R270:
      return {.x = logical.y, .y = h - logical.x};
  }
  return logical;
}

/// A native framebuffer (or touch sensor) point to the logical panel: the touch direction.
///
/// Exact inverse of to_native() for the same `native` and `r`, so a round trip returns the
/// input bit for bit.
///
/// @param native_pt a point in native pixels, as the touch controller reports it
/// @param native the framebuffer size
/// @param r the mounting rotation
/// @return the same place in the logical panel
[[nodiscard]] constexpr Point2 to_logical(Point2 native_pt, Size native, Rotation r) noexcept {
  const auto w = static_cast<float>(native.width);
  const auto h = static_cast<float>(native.height);
  switch (r) {
    case Rotation::R0:
      return native_pt;
    case Rotation::R90:
      return {.x = native_pt.y, .y = w - native_pt.x};
    case Rotation::R180:
      return {.x = w - native_pt.x, .y = h - native_pt.y};
    case Rotation::R270:
      return {.x = h - native_pt.y, .y = native_pt.x};
  }
  return native_pt;
}

/// The NanoVG transform for a frame: translate then rotate, applied after nvgBeginFrame so that
/// scene code draws in logical pixels. Radians are clockwise-positive in NanoVG's y-down space.
///
/// Apply as `nvgTranslate(translate_x, translate_y); nvgRotate(angle_rad);`. NanoVG composes
/// those so a point is rotated first and translated second, which reproduces to_native()
/// exactly; a test holds the two together.
struct FrameTransform {
  float translate_x = 0.0F;
  float translate_y = 0.0F;
  float angle_rad = 0.0F;
};

/// Builds the per-frame NanoVG transform that makes the native framebuffer look logical.
///
/// @param native the framebuffer size
/// @param r the mounting rotation
/// @return the translation and angle to apply before drawing the scene
[[nodiscard]] constexpr FrameTransform frame_transform(Size native, Rotation r) noexcept {
  const auto w = static_cast<float>(native.width);
  const auto h = static_cast<float>(native.height);
  constexpr float kHalfPi = std::numbers::pi_v<float> / 2.0F;
  switch (r) {
    case Rotation::R0:
      return {};
    case Rotation::R90:
      return {.translate_x = w, .translate_y = 0.0F, .angle_rad = kHalfPi};
    case Rotation::R180:
      return {.translate_x = w, .translate_y = h, .angle_rad = 2.0F * kHalfPi};
    case Rotation::R270:
      return {.translate_x = 0.0F, .translate_y = h, .angle_rad = -kHalfPi};
  }
  return {};
}

}  // namespace c3::platform
