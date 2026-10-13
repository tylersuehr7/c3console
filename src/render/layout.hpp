// Layout — every rect on the panel, computed once from the panel size.
//
// Proportions come from the LAYOUT table in mockups/c3console-ui-mockup.html, drawn at 2× on a
// 1600 × 2560 reference. Each region scales independently on its axis, so the layout stays
// proportional on other panels, and every coordinate is rounded to a whole pixel so hairlines
// stay crisp. Sub-layouts (tabs, dock, sheets, camera) scale their own mockup geometry from
// the rect they are given here.
#pragma once

#include "platform/display_backend.hpp"

namespace c3::render {

/// Reference panel the mockup is drawn at (800 × 1280 at 2×).
constexpr float kReferenceW = 1600.0F;
constexpr float kReferenceH = 2560.0F;

/// Width of the content column at the reference: the panel minus the 40-unit margins. The
/// status bar, well, tab bar, cluster and dock all span it, and the surfaces that fill the well
/// scale their geometry as `rect.w / kReferenceContentW`.
constexpr float kReferenceContentW = 1520.0F;

/// Corner radius of the well and of every surface drawn inside it, in reference units.
constexpr float kWellRadius = 28.0F;

/// Axis-aligned rectangle in panel pixels. Edges are half-open: `right()` and `bottom()` are the
/// first coordinates outside the rect.
struct Rect {
  float x = 0.0F;
  float y = 0.0F;
  float w = 0.0F;
  float h = 0.0F;

  [[nodiscard]] constexpr float right() const noexcept { return x + w; }
  [[nodiscard]] constexpr float bottom() const noexcept { return y + h; }
  [[nodiscard]] constexpr float cx() const noexcept { return x + w / 2; }
  [[nodiscard]] constexpr float cy() const noexcept { return y + h / 2; }

  /// Half-open containment: a point on the left or top edge is inside, one on the right or
  /// bottom edge is outside, as with pixel rects.
  [[nodiscard]] constexpr bool contains(float px, float py) const noexcept {
    return px >= x && py >= y && px < right() && py < bottom();
  }

  [[nodiscard]] constexpr bool operator==(const Rect&) const noexcept = default;
};

/// The top-level regions of the portrait panel, top to bottom.
struct Layout {
  platform::Size panel;
  float scale = 1.0F;  // panel px per reference unit, horizontal (panel.width / kReferenceW)
  Rect status;         // clock, brand mark, link chips and the theme button
  Rect well;           // CarPlay video surface; width and height are multiples of 16 px
  Rect tabs;           // surface tabs under the well
  Rect cluster;        // the gauge tiles below the tabs; the four tiles partition it
  Rect fuel;           // bar tile, top of the left column
  Rect systems;        // link chips, bottom of the left column
  Rect oil;            // arc tile, full cluster height
  Rect clt;            // arc tile, full cluster height
  Rect dock;           // Lights and HVAC islands
  Rect lever;          // hint line above the mechanical temperature lever

  /// Computes the layout for `panel`. A panel with a non-positive side yields all-zero rects.
  ///
  /// The well is the only region that does not simply scale: it fills the space between the
  /// status bar and the tabs, then its width and height are snapped down to multiples of 16 px
  /// and it is centred, so the video mode requested from the dongle needs no crop.
  ///
  /// @param panel the panel size in physical pixels
  /// @return the layout; every coordinate is a whole pixel
  [[nodiscard]] static Layout compute(platform::Size panel) noexcept;
};

}  // namespace c3::render
