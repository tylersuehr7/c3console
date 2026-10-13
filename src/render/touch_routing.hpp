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

/// Routes a single point. `carplay_active` is false while another surface occupies the well,
/// in which case every point is ours.
///
/// @return Well with well-local coordinates when the point is inside the well; otherwise Ui
///         with the panel coordinates unchanged
[[nodiscard]] constexpr RoutedTouch route_touch(const Layout& layout, float x, float y,
                                                bool carplay_active = true) noexcept {
  if (carplay_active && layout.well.contains(x, y)) {
    return {.target = TouchTarget::Well, .x = x - layout.well.x, .y = y - layout.well.y};
  }
  return {.target = TouchTarget::Ui, .x = x, .y = y};
}

struct RoutedGesture {
  TouchTarget target;
  platform::TouchEvent event;  // coordinates as RoutedTouch: well-local and clamped for Well
};

/// Captures one gesture at a time. The recipient is decided at Down and kept until Up or
/// Cancel: crossing the well boundary does not transfer the gesture, and a second finger while
/// one is down is ignored (spec §5.5). Hot path: no allocation.
class TouchRouter {
public:
  /// Records which surface occupies the well. Changing surface mid-gesture cancels the gesture:
  /// the returned event is the last routed one with its phase set to Cancel, and the finger's
  /// eventual Up is suppressed because it has no recipient.
  ///
  /// @param surface an opaque id for the occupant; any value, compared for change only
  /// @return the Cancel to deliver, or nullopt if nothing was in progress or nothing changed
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

  /// Routes one event. Down starts a gesture when none is active; Move, Up and Cancel are
  /// routed only for the active finger id. For a Well gesture the coordinates are made
  /// well-local and clamped inside the well, so CarPlay never sees a point outside its surface.
  ///
  /// @return the event to deliver with its target, or nullopt if it belongs to no gesture
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
  std::optional<RoutedGesture> active_;  // the captured gesture, holding its last routed event
};

}  // namespace c3::render
