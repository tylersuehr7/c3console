// Tracks the one touch contact the UI follows, so every event carries coordinates.
//
// libinput reports a finger going down and moving with coordinates, but a release carries
// none: the driver only says which slot lifted. The UI, and the CarPlay dongle the gesture
// may be forwarded to, both need to know where the finger was when it lifted. This object
// remembers the last known position of the active contact and stamps it onto Up and Cancel,
// so the input source can emit complete TouchEvents without the consumer keeping history.
//
// Policy: single contact, first finger wins. The console UI is single-touch and a CarPlay
// gesture is one finger, so a second finger that lands while one is already down is ignored
// entirely, along with its later moves and release, until the first finger lifts. A duplicate
// Down for the active finger is ignored too. Nothing here allocates or throws; it is called
// from the input poll on the render tick. Pure state machine; tested.
#pragma once

#include "input_source.hpp"

#include <optional>

namespace c3::platform {

class PanelTouchContact {
public:
  /// Feeds one raw contact report and returns the TouchEvent to deliver, if any.
  ///
  /// - Down: starts tracking `id` at (x, y) and returns the Down event. Returns nullopt when a
  ///   contact is already active, whichever finger it is.
  /// - Move: updates the position of the active contact and returns the Move event. Returns
  ///   nullopt when nothing is active or `id` is not the active finger.
  /// - Up / Cancel: ends tracking and returns the event stamped with the last known position.
  ///   `x` and `y` are ignored, because the hardware does not supply them for a release.
  ///   Returns nullopt when nothing is active or `id` is not the active finger.
  ///
  /// @param phase the raw report kind
  /// @param id the finger (libinput slot) the report is about; 0 for the mouse on the desktop
  /// @param x the reported panel x, meaningful for Down and Move only
  /// @param y the reported panel y, meaningful for Down and Move only
  /// @return the complete event to deliver, or nullopt when the report is dropped
  [[nodiscard]] std::optional<TouchEvent> update(TouchPhase phase, std::int32_t id, float x = 0, float y = 0) noexcept {
    if (phase == TouchPhase::Down) {
      if (last_) {
        return std::nullopt;
      }
      last_ = TouchEvent{.phase = phase, .id = id, .x = x, .y = y};
      return last_;
    }

    if (!last_ || last_->id != id) {
      return std::nullopt;
    }

    if (phase == TouchPhase::Move) {
      last_->x = x;
      last_->y = y;
      last_->phase = phase;
      return last_;
    }

    TouchEvent result = *last_;
    result.phase = phase;
    last_.reset();
    return result;
  }

  /// Ends the active contact from the host side: focus lost, device removed, source torn down.
  ///
  /// @return a Cancel event at the last known position of the active finger; nullopt when
  ///         nothing is active
  [[nodiscard]] std::optional<TouchEvent> cancel() noexcept {
    return last_ ? update(TouchPhase::Cancel, last_->id) : std::nullopt;
  }

private:
  std::optional<TouchEvent> last_;  // the active contact with its most recent position; empty when idle
};

}  // namespace c3::platform
