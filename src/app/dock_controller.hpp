// DockController — touch on the dock becomes HVAC and lamp commands; frames become the dock's
// picture.
//
// The dock only ever shows the relay state the MCU re-asserts in each frame (render/dock.hpp),
// so a tap submits a command and nothing more: the control lights up when the next frame says
// so. Without a live frame carrying relays every tap reads UNAVAILABLE, and a lamp tap is
// dropped while the frame leaves part of the lamp group unknown, because a lamp command carries
// the whole group. Hot path: no allocation.
#pragma once

#include "platform/input_source.hpp"
#include "render/dock.hpp"
#include "telemetry/frame.hpp"
#include "vehicle/vehicle_commands.hpp"

#include <optional>

namespace c3::app {

class DockController {
public:
  explicit DockController(vehicle::CommandSink& sink) noexcept : sink_(sink) {}

  /// Touch in panel pixels, already routed to the UI (not the well). A control activates on
  /// the release that lands on the same control it was pressed on.
  void on_touch(const platform::TouchEvent& ev, const render::DockLayout& dock) noexcept;

  /// The MCU's re-asserted relay state, when the frame carries it. A frame without `relays`
  /// marks the state unknown; optional doors and high-beam members are tracked separately.
  void apply_frame(const telemetry::Frame& frame) noexcept;

  /// Whether the link is live. Stale relay state is shown greyed out, never acted on.
  void set_live(bool live) noexcept { live_ = live; }

  /// What the dock draws: the MCU's state, the control under a finger, and the command status.
  [[nodiscard]] render::DockModel model() const noexcept {
    return {.hvac = hvac_,
            .lamps = lamps_,
            .pressed = pressed_,
            .live = live_ && relays_known_,
            .doors_known = doors_known_,
            .high_known = high_known_,
            .command = command_status()};
  }

private:
  void activate(render::DockControl c) noexcept;

  // The sink's status for the command in flight; otherwise the outcome of the last refused one.
  [[nodiscard]] vehicle::CommandStatus command_status() const noexcept {
    return submitted_ ? sink_.status(*submitted_) : last_status_;
  }

private:
  vehicle::CommandSink& sink_;
  std::optional<vehicle::CommandKind> submitted_;                      // last accepted command, whose status is shown
  vehicle::CommandStatus last_status_ = vehicle::CommandStatus::Idle;  // Unavailable or Rejected after a refusal
  bool live_ = false;
  bool relays_known_ = false;
  bool doors_known_ = false;
  bool high_known_ = false;
  vehicle::HvacState hvac_;
  vehicle::LampState lamps_;
  std::optional<render::DockControl> pressed_;
};

}  // namespace c3::app
