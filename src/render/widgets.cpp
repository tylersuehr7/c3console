#include "widgets.hpp"

#include "draw.hpp"
#include "settings.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace c3::render::widgets {
namespace {

constexpr float kStateTracking = 2.2F;  // .10em × 22
constexpr float kThumbRefD = 52.0F;     // mockup thumb diameter; its 5-unit ring scales with it
constexpr float kSwatchRefD = 84.0F;    // mockup .sw diameter; the selection ring scales with it

// Slider thumb: drop shadow, a ring in the sheet colour, then the disc.
void thumb(Canvas* vg, float cx, float cy, float d, Color fill, Color ring) noexcept {
  draw::circle_fill(vg, cx, cy + d * 0.08F, d / 2.0F + 4.0F, rgba(0x000000, 0.25F));  // the mockup's drop shadow
  draw::circle_fill(vg, cx, cy, d / 2.0F, ring);
  draw::circle_fill(vg, cx, cy, d / 2.0F - 5.0F * (d / kThumbRefD), fill);
}

// Horizontal travel of a thumb centre inside `r`: the half-thumb margins at both ends.
struct Travel {
  float x0;
  float x1;
};

[[nodiscard]] Travel thumb_travel(const Rect& r, float thumb_d) noexcept {
  return {.x0 = r.x + thumb_d / 2.0F, .x1 = r.right() - thumb_d / 2.0F};
}

}  // namespace

void split_segments(const Rect& group, float scale, std::span<Rect> out) noexcept {
  const auto n = static_cast<float>(out.size());
  const float pad = kSegmentPad * scale;
  const float gap = kSegmentGap * scale;
  const float bw = (group.w - 2 * pad - (n - 1) * gap) / n;
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = {
        .x = group.x + pad + static_cast<float>(i) * (bw + gap), .y = group.y + pad, .w = bw, .h = group.h - 2 * pad};
  }
}

void segment_button(Canvas* vg, const Fonts& f, const Theme& t, float scale, const Rect& r, std::string_view caption,
                    bool on, bool pressed) noexcept {
  if (on) {
    draw::rounded_fill(vg, r, kSegmentRadius * scale, t.ember);
  } else if (pressed) {
    draw::rounded_fill(vg, r, kSegmentRadius * scale, t.surface2);
  }

  draw::text(vg, on ? f.ui_semibold : f.ui_medium, 34.0F * scale, on ? t.ember_ink : t.ink2, draw::kCenterMiddle,
             r.cx(), r.cy(), caption);
}

void toggle(Canvas* vg, const Fonts& f, const Theme& t, float scale, const Rect& r, std::string_view title,
            std::string_view state, bool on, bool pressed, const Icon* icon) noexcept {
  draw::rounded_fill(vg, r, kControlRadius * scale, on ? t.ember : (pressed ? t.surface2 : t.surface));
  draw::rounded_stroke(vg, r, kControlRadius * scale, on ? t.ember : t.line);

  const Color ink = on ? t.ember_ink : t.ink2;
  float x = r.x + 30.0F * scale;

  if (icon != nullptr) {
    draw_icon(vg, *icon, x + 22.0F * scale, r.cy(), 44.0F * scale, ink, 2.2F);
    x += (44.0F + 22.0F) * scale;
  }

  draw::text(vg, on ? f.ui_semibold : f.ui_medium, 34.0F * scale, ink, draw::kLeftBaseline, x, r.cy() - 4.0F * scale,
             title);

  draw::text(vg, f.ui_medium, 22.0F * scale, on ? draw::alpha(t.ember_ink, 0.7F) : t.ink3, draw::kLeftBaseline, x,
             r.cy() + 30.0F * scale, state, kStateTracking * scale);
}

void icon_button(Canvas* vg, const Theme& t, const Rect& r, Icon icon, bool on, bool pressed, bool alarm_ink) noexcept {
  const float d = std::min(r.w, r.h);
  draw::circle_fill(vg, r.cx(), r.cy(), d / 2.0F, on ? t.ember : (pressed ? t.surface2 : t.surface));
  draw::circle_hairline(vg, r.cx(), r.cy(), d / 2.0F, on ? t.ember : t.line);
  const Color ink = on ? t.ember_ink : (alarm_ink ? t.alarm : t.ink2);
  draw_icon(vg, icon, r.cx(), r.cy(), d / 2.0F, ink);
}

void slider(Canvas* vg, const Theme& t, float scale, const Rect& r, float frac, const SliderStyle& style,
            Color sheet_bg) noexcept {
  const float cy = r.cy();
  const float th = style.track_h * scale;
  const float d = style.thumb_d * scale;
  const auto [x0, x1] = thumb_travel(r, d);
  const float tx = x0 + (x1 - x0) * std::clamp(frac, 0.0F, 1.0F);

  draw::rounded_fill(vg, {.x = x0, .y = cy - th / 2.0F, .w = x1 - x0, .h = th}, th / 2.0F, t.line2);

  if (style.fill_from_start && tx > x0) {
    draw::rounded_fill(vg, {.x = x0, .y = cy - th / 2.0F, .w = tx - x0, .h = th}, th / 2.0F, t.ember);
  }

  if (style.centre_tick) {
    draw::rect_fill(
        vg, {.x = r.cx() - 1.0F * scale, .y = cy + th / 2.0F + 4.0F * scale, .w = 2.0F * scale, .h = 16.0F * scale},
        t.ink3);
  }

  thumb(vg, tx, cy, d, t.ink, sheet_bg);
}

void hue_track(Canvas* vg, float scale, const Rect& r, float frac, Color thumb_color, Color sheet_bg) noexcept {
  const float cy = r.cy();
  const float th = 16.0F * scale;
  const float d = kThumbRefD * scale;
  const auto [x0, x1] = thumb_travel(r, d);
  // Six gradient stops, drawn as adjoining segments under a clip.
  {
    const draw::State st(vg);
    st.clip({.x = x0, .y = cy - th / 2.0F, .w = x1 - x0, .h = th});
    constexpr int kStops = 6;
    for (int i = 0; i < kStops; ++i) {
      const float a = x0 + (x1 - x0) * static_cast<float>(i) / kStops;
      const float b = x0 + (x1 - x0) * static_cast<float>(i + 1) / kStops;
      draw::rect_fill(vg, {.x = a, .y = cy - th / 2.0F, .w = b - a + 1.0F, .h = th},
                      draw::linear(vg, a, cy, b, cy, rgb(accent_from_hue(static_cast<float>(i) * 60.0F)),
                                   rgb(accent_from_hue(static_cast<float>(i + 1) * 60.0F))));
    }
  }
  thumb(vg, x0 + (x1 - x0) * std::clamp(frac, 0.0F, 1.0F), cy, d, thumb_color, sheet_bg);
}

void swatch(Canvas* vg, const Theme& t, const Rect& r, Color color, bool on) noexcept {
  const float d = std::min(r.w, r.h);
  draw::circle_fill(vg, r.cx(), r.cy(), d / 2.0F, color);
  draw::circle_stroke(vg, r.cx(), r.cy(), d / 2.0F - 1.0F, 2.0F, rgba(0x000000, 0.18F));
  if (on) {
    const float k = d / kSwatchRefD;
    draw::circle_stroke(vg, r.cx(), r.cy(), d / 2.0F + 7.0F * k, 4.0F * k, t.ink);
  }
}

float slider_fraction(const Rect& r, float scale, const SliderStyle& style, float x) noexcept {
  const auto [x0, x1] = thumb_travel(r, style.thumb_d * scale);
  return x1 > x0 ? std::clamp((x - x0) / (x1 - x0), 0.0F, 1.0F) : 0.0F;
}

}  // namespace c3::render::widgets
