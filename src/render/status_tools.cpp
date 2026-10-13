#include "status_tools.hpp"

#include "widgets.hpp"

namespace c3::render {
namespace {

constexpr float kRefBarH = 88.0F;
constexpr float kButton = 64.0F;
constexpr float kGap = 12.0F;

}  // namespace

ToolsLayout ToolsLayout::compute(const Rect& status) noexcept {
  ToolsLayout tl{.scale = status.h > 0 ? status.h / kRefBarH : 1.0F, .buttons = {}};
  const float s = tl.scale;
  const float d = kButton * s;
  float x = status.right() - d;
  for (std::size_t i = kToolCount; i-- > 0;) {
    tl.buttons[i] = {.x = x, .y = status.cy() - d / 2.0F, .w = d, .h = d};
    x -= d + kGap * s;
  }
  return tl;
}

std::optional<Tool> ToolsLayout::hit_test(float x, float y) const noexcept {
  for (std::size_t i = 0; i < kToolCount; ++i) {
    if (buttons[i].contains(x, y)) {
      return static_cast<Tool>(i);
    }
  }
  return std::nullopt;
}

void draw_status_tools(Canvas* vg, const ToolsLayout& tl, const Theme& t, const ToolsModel& m) noexcept {
  widgets::icon_button(vg, t, tl.rect(Tool::Theme), m.dark ? Icon::Sun : Icon::Moon, false, m.pressed == Tool::Theme);
}

}  // namespace c3::render
