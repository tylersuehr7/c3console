#include "tabs.hpp"

#include "draw.hpp"
#include "icons.hpp"

#include <array>
#include <string_view>

namespace c3::render {
namespace {

constexpr float kRefBarH = 64.0F;
constexpr float kPad = 6.0F;
constexpr float kGap = 6.0F;
constexpr float kTabH = 52.0F;
constexpr float kIcon = 26.0F;
constexpr float kPadLeft = 26.0F;
constexpr float kIconGap = 12.0F;
constexpr float kDot = 12.0F;
constexpr float kTracking = 0.7F;  // .03em × 24

struct TabSpec {
  std::string_view caption;
  Icon icon;
  float width;  // mockup units, sized for the caption at 24 px plus the Camera tab's dot
};
constexpr std::array<TabSpec, kTabCount> kTabs = {{
    {"CarPlay", Icon::Phone, 188.0F},
    {"Camera", Icon::Camera, 204.0F},
    {"Settings", Icon::Gear, 190.0F},
}};

}  // namespace

TabsLayout TabsLayout::compute(const Rect& tabs) noexcept {
  TabsLayout tl{.scale = tabs.h > 0 ? tabs.h / kRefBarH : 1.0F, .pill = {}, .buttons = {}};

  const float s = tl.scale;
  float total = 2 * kPad + static_cast<float>(kTabCount - 1) * kGap;
  for (const TabSpec& spec : kTabs) {
    total += spec.width;
  }

  tl.pill = {.x = tabs.cx() - total * s / 2, .y = tabs.y, .w = total * s, .h = kRefBarH * s};
  float x = tl.pill.x + kPad * s;
  for (std::size_t i = 0; i < kTabCount; ++i) {
    tl.buttons[i] = {.x = x, .y = tl.pill.y + kPad * s, .w = kTabs[i].width * s, .h = kTabH * s};
    x += (kTabs[i].width + kGap) * s;
  }

  return tl;
}

std::optional<Tab> TabsLayout::hit_test(float x, float y) const noexcept {
  for (std::size_t i = 0; i < kTabCount; ++i) {
    if (buttons[i].contains(x, y)) {
      return static_cast<Tab>(i);
    }
  }
  return std::nullopt;
}

void draw_tabs(Canvas* vg, const TabsLayout& tl, const Theme& t, const Fonts& f, const TabsModel& m) noexcept {
  const float s = tl.scale;
  draw::rounded_fill(vg, tl.pill, tl.pill.h / 2, t.surface);
  draw::rounded_stroke(vg, tl.pill, tl.pill.h / 2, t.line);
  for (std::size_t i = 0; i < kTabCount; ++i) {
    const auto tab = static_cast<Tab>(i);
    const Rect& r = tl.buttons[i];
    const bool on = m.active == tab;

    if (on) {
      draw::rounded_fill(vg, r, r.h / 2, t.ember);
    } else if (m.pressed == tab) {
      draw::rounded_fill(vg, r, r.h / 2, t.surface2);
    }

    const Color ink = on ? t.ember_ink : t.ink2;
    const float ix = r.x + kPadLeft * s + kIcon * s / 2;
    draw_icon(vg, kTabs[i].icon, ix, r.cy(), kIcon * s, ink, 2.1F);

    const float tx = r.x + (kPadLeft + kIcon + kIconGap) * s;
    draw::text(vg, on ? f.ui_semibold : f.ui_medium, 24.0F * s, ink, draw::kLeftMiddle, tx, r.cy(), kTabs[i].caption,
               kTracking * s);

    if (tab == Tab::Camera && m.reverse) {
      const float cx = r.right() - (kPadLeft - 2.0F) * s;
      draw::circle_fill(
          vg, cx, r.cy(), kDot * s,
          draw::radial(vg, cx, r.cy(), kDot * s / 2, kDot * s, draw::alpha(t.alarm, 0.6F), draw::alpha(t.alarm, 0)));
      draw::circle_fill(vg, cx, r.cy(), kDot * s / 2, t.alarm);
    }
  }
}

}  // namespace c3::render
