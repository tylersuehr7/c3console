// Every rect on the panel, computed once from the panel size.
//
// Proportions come from the LAYOUT table in mockups/c3console-ui-mockup.html.
// Drawn at 2× for the 800 × 1280 panel (reference 1600 × 2560): 40 px side margin,
// 88 px status bar, the CarPlay well on top as the hero, a 64 px tab bar under it,
// a 528 px cluster (fuel and systems cards on the left, the two arc gauges to their
// right), a 392 px control dock of two islands and a 36 px lever hint. Vertical measures
// scale with panel height, horizontal with width, so the reference panel reproduces the
// mockup exactly and other panels keep the same structure. The well is snapped to 16
// px on both axes so the H.264 mode requested from the dongle needs no crop. `scale`
// is what text and radii multiply by.
#pragma once

#include "platform/display_backend.hpp"

namespace c3::render {

/// Reference panel the mockup is drawn at (800 × 1280 at 2×).
constexpr float kReferenceW = 1600.0F;
constexpr float kReferenceH = 2560.0F;

struct Rect {
  float x = 0.0F;
  float y = 0.0F;
  float w = 0.0F;
  float h = 0.0F;

  [[nodiscard]] constexpr float right() const noexcept { return x + w; }
  [[nodiscard]] constexpr float bottom() const noexcept { return y + h; }
  [[nodiscard]] constexpr float cx() const noexcept { return x + w / 2; }
  [[nodiscard]] constexpr float cy() const noexcept { return y + h / 2; }

  /// Half-open: the right and bottom edges are outside, as with pixel rects.
  [[nodiscard]] constexpr bool contains(float px, float py) const noexcept {
    return px >= x && py >= y && px < right() && py < bottom();
  }

  [[nodiscard]] constexpr bool operator==(const Rect&) const noexcept = default;
};

struct Layout {
  platform::Size panel;
  float scale = 1.0F;  // panel px per reference unit, horizontal
  Rect status;
  Rect well;  // CarPlay surface; 16-px aligned
  Rect tabs;  // surface tabs under the well
  Rect cluster;
  Rect fuel;     // bar tile
  Rect systems;  // link chips
  Rect oil;      // arc tile
  Rect clt;      // arc tile
  Rect dock;
  Rect lever;

  /// All coordinates are whole pixels.
  [[nodiscard]] static Layout compute(platform::Size panel) noexcept;
};

}  // namespace c3::render
