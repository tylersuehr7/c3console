#pragma once

#include "canvas.hpp"
#include "fonts.hpp"
#include "layout.hpp"
#include "theme.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace c3::render {

enum class Tab : std::uint8_t {
  CarPlay,
  Camera,
  Settings,
  Count,
};
constexpr std::size_t kTabCount = static_cast<std::size_t>(Tab::Count);

[[nodiscard]] constexpr std::string_view to_string(Tab t) noexcept {
  switch (t) {
    case Tab::CarPlay:
      return "carplay";
    case Tab::Camera:
      return "camera";
    case Tab::Settings:
      return "settings";
    case Tab::Count:
      break;
  }
  return "?";
}

struct TabsLayout {
  float scale = 1.0F;  // pixels per mockup unit (64-unit bar)
  Rect pill;
  std::array<Rect, kTabCount> buttons;

  /// Centred in `tabs`: a 64-unit pill of 52-unit buttons with 6-unit padding and gaps.
  [[nodiscard]] static TabsLayout compute(const Rect& tabs) noexcept;
  [[nodiscard]] std::optional<Tab> hit_test(float x, float y) const noexcept;
  [[nodiscard]] const Rect& rect(Tab t) const noexcept { return buttons[static_cast<std::size_t>(t)]; }
};

struct TabsModel {
  Tab active = Tab::CarPlay;
  bool reverse = false;  // red dot on the Camera tab while the car owns the well
  std::optional<Tab> pressed;
};

void draw_tabs(Canvas* vg, const TabsLayout& tl, const Theme& t, const Fonts& f, const TabsModel& m) noexcept;

}  // namespace c3::render
