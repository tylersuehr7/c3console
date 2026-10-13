#pragma once

#include "canvas.hpp"
#include "fonts.hpp"
#include "layout.hpp"
#include "theme.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace c3::render {

enum class Tool : std::uint8_t { Theme, Count };
constexpr std::size_t kToolCount = static_cast<std::size_t>(Tool::Count);

struct ToolsLayout {
  float scale = 1.0F;  // status-bar pixels per mockup unit (88-px reference bar)
  std::array<Rect, kToolCount> buttons;

  /// Right-aligned in `status`, 64-unit buttons with 12-unit gaps.
  [[nodiscard]] static ToolsLayout compute(const Rect& status) noexcept;
  [[nodiscard]] std::optional<Tool> hit_test(float x, float y) const noexcept;
  [[nodiscard]] const Rect& rect(Tool t) const noexcept { return buttons[static_cast<std::size_t>(t)]; }

  /// Left edge of the group: where the rest of the status bar's right side ends.
  [[nodiscard]] float left() const noexcept { return buttons.front().x; }
};

struct ToolsModel {
  bool dark = true;  // the theme button shows the sun on dark and the moon on light
  std::optional<Tool> pressed;
};

void draw_status_tools(Canvas* vg, const ToolsLayout& tl, const Theme& t, const ToolsModel& m) noexcept;

}  // namespace c3::render
