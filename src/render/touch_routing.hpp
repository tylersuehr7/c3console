// Touch routing (spec §5.5): a point inside the CarPlay well is forwarded to the dongle in
// well-local coordinates; anything else is ours. The well rect is half-open, so a touch
// exactly on its right or bottom edge is ours and one on its left or top edge is CarPlay's.
// While another surface (camera, a sheet) occupies the well, nothing is forwarded: those
// surfaces consume their own touches (docs/c3-console-ui-design.md, "surface slot").
#pragma once

#include "layout.hpp"
#include "platform/input_source.hpp"

#include <algorithm>
#include <optional>

namespace c3::render {

enum class TouchTarget {
  Well,  // forward to CarPlay
  Ui,    // dispatch locally
};

struct RoutedTouch {
  TouchTarget target = TouchTarget::Ui;
  float x = 0.0F;  // well-local when target == Well, panel pixels otherwise
  float y = 0.0F;
};

[[nodiscard]] constexpr RoutedTouch route_touch(const Layout& layout, float x, float y,
                                                bool carplay_active = true) noexcept {
  if (carplay_active && layout.well.contains(x, y)) {
    return {.target = TouchTarget::Well, .x = x - layout.well.x, .y = y - layout.well.y};
  }
  return {.target = TouchTarget::Ui, .x = x, .y = y};
}

struct RoutedGesture {
  TouchTarget target;
  platform::TouchEvent event;
};

class TouchRouter {
public:
  // A surface takeover cancels the old gesture; its later release has no new recipient.
  [[nodiscard]] std::optional<RoutedGesture> set_surface(unsigned surface) noexcept {
    if (surface == surface_) {
      return std::nullopt;
    }

    surface_ = surface;
    if (!active_) {
      return std::nullopt;
    }

    auto cancelled = active_;
    cancelled->event.phase = platform::TouchPhase::Cancel;
    active_.reset();
    return cancelled;
  }

  [[nodiscard]] std::optional<RoutedGesture> route(const Layout& layout, const platform::TouchEvent& event,
                                                   bool carplay_active) noexcept {
    if (event.phase == platform::TouchPhase::Down) {
      if (active_) {
        return std::nullopt;
      }
      active_ = RoutedGesture{.target = route_touch(layout, event.x, event.y, carplay_active).target, .event = event};
    } else if (!active_ || active_->event.id != event.id) {
      return std::nullopt;
    }

    auto result = *active_;
    result.event = event;
    if (result.target == TouchTarget::Well) {
      result.event.x = std::clamp(event.x - layout.well.x, 0.0F, layout.well.w - 1.0F);
      result.event.y = std::clamp(event.y - layout.well.y, 0.0F, layout.well.h - 1.0F);
    }

    active_ = result;
    if (event.phase == platform::TouchPhase::Up || event.phase == platform::TouchPhase::Cancel) {
      active_.reset();
    }

    return result;
  }

private:
  unsigned surface_ = 0;
  std::optional<RoutedGesture> active_;
};

}  // namespace c3::render
