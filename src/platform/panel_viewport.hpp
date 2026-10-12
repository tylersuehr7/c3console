// Fits the fixed-size logical panel into whatever drawable the backend has.
//
// The renderer always draws in panel pixels (1668 by 2388 on the target). On a laptop the
// window is smaller and the panel is scaled to fit with letterboxing; on the target the
// drawable is the panel and the scale is 1. Touch input runs the same mapping in reverse, so
// a tap on a drawn control lands on that control at any window size.
//
// Three coordinate spaces meet here:
// - Panel pixels: what scene code draws in and what TouchEvents carry. Fixed per build.
// - Drawable pixels: the GL backing store the frame is rendered into.
// - Window points: what SDL reports for the mouse and for touch. On a HiDPI display one
//   window point is several drawable pixels; pixel_ratio() is that factor.
//
// The panel is centred along the axis that has room to spare and flush along the axis that
// limits the scale, so exactly one offset is zero. Everything here is constexpr, allocation
// free and exception free. Pure; tested.
#pragma once

#include "display_backend.hpp"

#include <algorithm>
#include <optional>

namespace c3::platform {

/// A point in panel pixels, the space scene code and TouchEvents use.
struct PanelPoint {
  float x = 0.0F;  // panel pixels
  float y = 0.0F;
};

/// The mapping between panel, drawable and window for one frame. Built by fit_panel().
struct PanelViewport {
  Size panel;          // logical panel, pixels
  int drawable_w = 0;  // backing store, pixels
  int drawable_h = 0;
  int window_w = 0;  // window, points (differs from drawable on HiDPI)
  int window_h = 0;
  float scale = 1.0F;     // drawable pixels per panel pixel
  float offset_x = 0.0F;  // letterbox origin in drawable pixels
  float offset_y = 0.0F;

  /// Drawable pixels per window point. 2.0 on a Retina display, 1.0 otherwise.
  ///
  /// @return the ratio, or 1.0 when the window size is unknown so points are taken as pixels
  [[nodiscard]] constexpr float pixel_ratio() const noexcept {
    return window_w > 0 ? static_cast<float>(drawable_w) / static_cast<float>(window_w) : 1.0F;
  }
};

/// Largest uniform scale that fits `panel` inside the drawable, centred.
///
/// The scale is the smaller of the two axis ratios. The limiting axis is flush with an exact
/// zero offset; the other axis is centred with the leftover split evenly. A non-positive panel
/// or drawable size leaves the identity mapping (scale 1, no offset) so nothing divides by zero.
///
/// @param panel the logical panel size
/// @param drawable_w the backing store width in pixels
/// @param drawable_h the backing store height in pixels
/// @param window_w the window width in points, for pixel_ratio()
/// @param window_h the window height in points
/// @return the viewport to render and route touches through until the window changes
[[nodiscard]] constexpr PanelViewport fit_panel(Size panel, int drawable_w, int drawable_h, int window_w,
                                                int window_h) noexcept {
  PanelViewport vp{
      .panel = panel,
      .drawable_w = drawable_w,
      .drawable_h = drawable_h,
      .window_w = window_w,
      .window_h = window_h,
  };

  if (panel.width <= 0 || panel.height <= 0 || drawable_w <= 0 || drawable_h <= 0) {
    return vp;
  }

  const float sx = static_cast<float>(drawable_w) / static_cast<float>(panel.width);
  const float sy = static_cast<float>(drawable_h) / static_cast<float>(panel.height);

  // The limiting axis gets an exact zero offset; the other is centred. Computing both from
  // the shared scale would leave a rounding residue on the axis that should be flush.
  if (sx <= sy) {
    vp.scale = sx;
    vp.offset_x = 0.0F;
    vp.offset_y = (static_cast<float>(drawable_h) - static_cast<float>(panel.height) * sx) / 2;
  } else {
    vp.scale = sy;
    vp.offset_y = 0.0F;
    vp.offset_x = (static_cast<float>(drawable_w) - static_cast<float>(panel.width) * sy) / 2;
  }

  return vp;
}

/// Drawable pixel -> panel pixel. nullopt when the point lands in the letterbox or outside.
///
/// The panel is half-open: 0 is inside, width and height are outside. Use this for Down, where
/// a touch in the letterbox must start nothing.
///
/// @param vp the current viewport
/// @param x the drawable x in pixels
/// @param y the drawable y in pixels
/// @return the panel point, or nullopt when the touch missed the panel or `vp` has no scale
[[nodiscard]] constexpr std::optional<PanelPoint> drawable_to_panel(const PanelViewport& vp, float x,
                                                                    float y) noexcept {
  if (vp.scale <= 0.0F) {
    return std::nullopt;
  }

  const PanelPoint p{.x = (x - vp.offset_x) / vp.scale, .y = (y - vp.offset_y) / vp.scale};

  const bool inside = p.x >= 0.0F && p.y >= 0.0F && p.x < static_cast<float>(vp.panel.width) &&
                      p.y < static_cast<float>(vp.panel.height);

  return inside ? std::optional<PanelPoint>{p} : std::nullopt;
}

/// Drawable pixel -> nearest panel pixel, for releases and drags that leave the panel.
///
/// A gesture that started on the panel must finish with coordinates the panel can use, even
/// when the finger slid into the letterbox or off the window. Results are clamped to the last
/// pixel, width - 1 and height - 1, never to the size itself.
///
/// @param vp the current viewport
/// @param x the drawable x in pixels
/// @param y the drawable y in pixels
/// @return the nearest panel pixel; the origin when `vp` has no scale
[[nodiscard]] constexpr PanelPoint drawable_to_panel_clamped(const PanelViewport& vp, float x, float y) noexcept {
  if (vp.scale <= 0.0F) {
    return {};
  }

  const float max_x = static_cast<float>(vp.panel.width) - 1.0F;
  const float max_y = static_cast<float>(vp.panel.height) - 1.0F;

  return {.x = std::clamp((x - vp.offset_x) / vp.scale, 0.0F, max_x),
          .y = std::clamp((y - vp.offset_y) / vp.scale, 0.0F, max_y)};
}

/// Window point (as SDL reports mouse and touch) -> panel pixel.
///
/// Scales the point by pixel_ratio() into drawable pixels first, then maps as drawable_to_panel().
///
/// @param vp the current viewport
/// @param x the window x in points
/// @param y the window y in points
/// @return the panel point, or nullopt when the touch missed the panel
[[nodiscard]] constexpr std::optional<PanelPoint> window_to_panel(const PanelViewport& vp, float x, float y) noexcept {
  const float r = vp.pixel_ratio();
  return drawable_to_panel(vp, x * r, y * r);
}

/// Window point -> nearest panel pixel, the clamped counterpart of window_to_panel().
///
/// @param vp the current viewport
/// @param x the window x in points
/// @param y the window y in points
/// @return the nearest panel pixel
[[nodiscard]] constexpr PanelPoint window_to_panel_clamped(const PanelViewport& vp, float x, float y) noexcept {
  const float r = vp.pixel_ratio();
  return drawable_to_panel_clamped(vp, x * r, y * r);
}

}  // namespace c3::platform
