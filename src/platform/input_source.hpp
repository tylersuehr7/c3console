// InputSource — the seam between touch hardware and the UI.
//
// Desktop: SdlInput (mouse and touch through SDL). Target: LibinputSource.
//
// Events arrive in panel pixels. The transform from device coordinates is the source's job
// (platform/viewport.hpp). The touch router captures each gesture at Down according to whether it
// lands in the CarPlay well and is forwarded to the dongle, or is handled by our UI (spec §5.5).
//
// Hot path: no allocation, no exceptions.
#pragma once

#include <cstdint>
#include <string_view>

namespace c3::platform {

enum class TouchPhase : std::uint8_t { Down, Move, Up, Cancel };

[[nodiscard]] constexpr std::string_view to_string(TouchPhase phase) noexcept {
  switch (phase) {
    case TouchPhase::Down:
      return "down";
    case TouchPhase::Move:
      return "move";
    case TouchPhase::Up:
      return "up";
    case TouchPhase::Cancel:
      return "cancel";
  }
  return "?";
}

struct TouchEvent {
  TouchPhase phase = TouchPhase::Down;
  std::int32_t id = 0;  // finger id; 0 for the mouse
  float x = 0.0F;       // panel pixels
  float y = 0.0F;
};

class InputSource {
public:
  virtual ~InputSource() = default;

  InputSource(const InputSource&) = delete;
  InputSource& operator=(const InputSource&) = delete;
  InputSource(InputSource&&) = delete;
  InputSource& operator=(InputSource&&) = delete;

  /// Fills `out` with the next event and returns true, or returns false when none is pending.
  /// Drains device events as a side effect, so call until it returns false each tick.
  [[nodiscard]] virtual bool poll(TouchEvent& out) noexcept = 0;

  /// The user or the platform asked the application to exit (window close, Escape, SIGTERM
  /// relayed by the platform).
  [[nodiscard]] virtual bool quit_requested() const noexcept = 0;

protected:
  InputSource() = default;
};

}  // namespace c3::platform
