#include "layout.hpp"

#include <cmath>

namespace c3::render {

namespace {

// Reference geometry (mockup, 1600 × 2560). Vertical positions scale with the panel height,
// horizontal ones with the width.
constexpr float kMargin = 40.0F;
constexpr float kStatusH = 88.0F;
constexpr float kWellY = 100.0F;
constexpr float kWellGap = 12.0F;  // well -> tabs
constexpr float kTabsY = 1504.0F;
constexpr float kTabsH = 64.0F;
constexpr float kClusterY = 1584.0F;
constexpr float kClusterH = 528.0F;
constexpr float kDockY = 2128.0F;
constexpr float kDockH = 392.0F;
constexpr float kLeverH = 36.0F;
constexpr float kTileGap = 16.0F;
constexpr float kAlign = 16.0F;  // well snap, both axes

static_assert(kReferenceW - 2 * kMargin == kReferenceContentW);

/// Rounds to a whole pixel.
[[nodiscard]] float px(float v) noexcept {
  return std::round(v);
}

/// Largest multiple of kAlign not above `v`.
[[nodiscard]] float align_down(float v) noexcept {
  return std::floor(v / kAlign) * kAlign;
}

/// A rect with every coordinate rounded to a whole pixel.
[[nodiscard]] Rect rounded(float x, float y, float w, float h) noexcept {
  return {.x = px(x), .y = px(y), .w = px(w), .h = px(h)};
}

}  // namespace

Layout Layout::compute(platform::Size panel) noexcept {
  Layout l{.panel = panel};

  if (panel.width <= 0 || panel.height <= 0) {
    return l;
  }

  const auto w = static_cast<float>(panel.width);
  const auto h = static_cast<float>(panel.height);
  const float sx = w / kReferenceW;
  const float sy = h / kReferenceH;
  l.scale = sx;

  const float margin = px(kMargin * sx);
  const float content_w = w - 2 * margin;
  const float tile_gap = px(kTileGap * sx);

  l.status = rounded(margin, 0, content_w, kStatusH * sy);
  l.tabs = rounded(margin, kTabsY * sy, content_w, kTabsH * sy);
  l.cluster = rounded(margin, kClusterY * sy, content_w, kClusterH * sy);
  l.dock = rounded(margin, kDockY * sy, content_w, kDockH * sy);
  l.lever = rounded(0, h - px(kLeverH * sy), w, kLeverH * sy);

  // The well fills the space between the status bar and the tabs, snapped to 16 px.
  const float well_top = px(kWellY * sy);
  const float well_avail_h = l.tabs.y - px(kWellGap * sy) - well_top;
  const float well_w = align_down(content_w);
  const float well_h = align_down(well_avail_h);
  l.well = rounded(margin + (content_w - well_w) / 2, well_top, well_w, well_h);

  const float tile_w = px((content_w - 2 * tile_gap) / 3);
  const float mini_h = px((l.cluster.h - tile_gap) / 2);
  l.fuel = rounded(margin, l.cluster.y, tile_w, mini_h);
  // The second small card takes the remainder so its bottom edge meets the arcs' exactly.
  l.systems = rounded(margin, l.cluster.y + mini_h + tile_gap, tile_w, l.cluster.h - mini_h - tile_gap);
  l.oil = rounded(margin + tile_w + tile_gap, l.cluster.y, tile_w, l.cluster.h);
  l.clt = rounded(margin + 2 * (tile_w + tile_gap), l.cluster.y, tile_w, l.cluster.h);

  return l;
}

}  // namespace c3::render
