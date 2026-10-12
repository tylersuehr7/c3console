// SdlInput — mouse and touch from SDL, mapped into panel pixels through the backend's
// panel viewport. Also carries the quit request (window close, Escape).
//
// A press must start inside the panel to be reported. While pressed, moves and the release
// are clamped to the panel edge rather than dropped, so the UI always sees the Up that
// matches a Down.
#pragma once

#include "desktop_display_backend.hpp"
#include "input_source.hpp"
#include "panel_touch_contact.hpp"

#include <cstdint>
#include <optional>

namespace c3::platform {

class SdlInputSource : public InputSource {
public:
  explicit SdlInputSource(const DesktopDisplayBackend& backend) noexcept : backend_(backend) {}

  [[nodiscard]] bool poll(TouchEvent& out) noexcept override;
  [[nodiscard]] bool quit_requested() const noexcept override { return quit_; }

private:
  /// Maps a window-point event to a TouchEvent, applying the press rules above.
  /// Returns nullopt when the event should not be reported.
  [[nodiscard]] std::optional<TouchEvent> map(TouchPhase phase, std::int32_t id, float wx, float wy) noexcept;

private:
  const DesktopDisplayBackend& backend_;
  bool quit_ = false;
  PanelTouchContact contact_;
};

}  // namespace c3::platform
