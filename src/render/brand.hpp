// Brand mark — the Suehr Studios "S".
#pragma once

#include "canvas.hpp"

namespace c3::render {

/// Natural size of the mark's coordinate space.
constexpr float kBrandW = 408.0F;
constexpr float kBrandH = 262.0F;

/// Draws the mark with its top-left at (x, y) scaled to `height`, filled with `color`.
void draw_brand_mark(Canvas* vg, float x, float y, float height, Color color) noexcept;

/// Width the mark takes at `height`.
[[nodiscard]] constexpr float brand_width(float height) noexcept {
  return height * kBrandW / kBrandH;
}

}  // namespace c3::render
