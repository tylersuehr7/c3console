// LibinputInputSource — touch from the panel's USB digitizer through libinput + udev.
//
// One finger is tracked: the first slot down owns the gesture until it lifts; other fingers
// are ignored, which is what the dock and the sheets expect. Coordinates come from libinput
// scaled to the native panel resolution and are turned into logical panel pixels through
// platform/panel_rotation.hpp, the same transform DrmBackend draws with.
//
// Hot path: no allocation; the pending events sit in a fixed ring.
#pragma once

#include "display_backend.hpp"
#include "input_source.hpp"
#include "panel_rotation.hpp"
#include "panel_touch_contact.hpp"

#include <libinput.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

// Sourced from <nanovg.h>
struct udev;
struct libinput;

namespace c3::platform {

class LibinputInputSource final : public InputSource {
public:
  struct Options {
    Size native{};  // the panel's native (unrotated) resolution in pixels
    Rotation rotation = Rotation::R0;
    std::string seat = "seat0";
  };

  struct Result {
    std::unique_ptr<LibinputInputSource> source;  // null when `error` is set
    std::string error;
  };

  /// Creates the udev context and libinput, assigns the seat. Init-time; needs read access to
  /// /dev/input (the service runs in the `input` group).
  [[nodiscard]] static Result create(const Options& options);

  ~LibinputInputSource() override;
  LibinputInputSource(const LibinputInputSource&) = delete;
  LibinputInputSource& operator=(const LibinputInputSource&) = delete;
  LibinputInputSource(LibinputInputSource&&) = delete;
  LibinputInputSource& operator=(LibinputInputSource&&) = delete;

  [[nodiscard]] bool poll(TouchEvent& out) noexcept override;
  [[nodiscard]] bool quit_requested() const noexcept override { return false; }
  [[nodiscard]] int descriptor() const noexcept;
  [[nodiscard]] std::uint32_t dropped() const noexcept { return dropped_; }

private:
  LibinputInputSource(Size native, Rotation rotation) noexcept : native_(native), rotation_(rotation) {}
  void drain_device() noexcept;
  void handle_touch(libinput_event_type type, libinput_event_touch* t) noexcept;
  void push(const TouchEvent& event) noexcept;

private:
  Size native_;
  Rotation rotation_;
  udev* udev_ = nullptr;
  libinput* li_ = nullptr;
  PanelTouchContact contact_;

  static constexpr std::size_t kRing = 64;
  std::array<TouchEvent, kRing> ring_{};
  std::size_t head_ = 0;
  std::size_t tail_ = 0;
  std::uint32_t dropped_ = 0;
};

}  // namespace c3::platform
