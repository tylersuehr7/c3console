#include "dock_controller.hpp"

namespace c3::app {

using render::DockControl;

// A press is a candidate until the release lands on the same control; moving off and back
// still activates, lifting elsewhere or a cancel does not.

void DockController::on_touch(const platform::TouchEvent& ev, const render::DockLayout& dock) noexcept {
  const std::optional<DockControl> hit = dock.hit_test(ev.x, ev.y);
  switch (ev.phase) {
    case platform::TouchPhase::Down:
      pressed_ = hit;
      break;
    case platform::TouchPhase::Move:
      break;  // pressed_ stays; release decides
    case platform::TouchPhase::Cancel:
      pressed_.reset();
      break;
    case platform::TouchPhase::Up:
      if (pressed_ && hit == pressed_) {
        activate(*pressed_);
      }
      pressed_.reset();
      break;
  }
}

void DockController::apply_frame(const telemetry::Frame& frame) noexcept {
  relays_known_ = frame.relays.has_value();
  if (frame.relays) {
    hvac_ = {.mode = frame.relays->mode, .blower = frame.relays->blower, .ac_enabled = frame.relays->ac_enabled};
    lamps_.headlights_on = frame.relays->headlights_on;
    doors_known_ = frame.relays->lamp_doors_open.has_value();

    if (frame.relays->lamp_doors_open) {
      lamps_.lamp_doors_open = *frame.relays->lamp_doors_open;
    }

    high_known_ = frame.relays->highbeams_on.has_value();
    if (frame.relays->highbeams_on) {
      lamps_.highbeams_on = *frame.relays->highbeams_on;
    }
  }
}

// Builds the requested state from the control and the MCU's current state, then submits it.
// Every edit keeps the state valid (vehicle::is_valid) by cascading the way the car's own
// switches do: lights raise the doors, closing the doors drops the lights, high beams need
// the lights. Nothing is applied locally; the next frame's relays are the truth.
void DockController::activate(DockControl c) noexcept {
  if (!live_ || !relays_known_) {
    last_status_ = vehicle::CommandStatus::Unavailable;
    submitted_.reset();
    return;
  }

  vehicle::HvacState h = hvac_;
  vehicle::LampState l = lamps_;
  switch (c) {
    case DockControl::BlowerOff:
    case DockControl::Blower1:
    case DockControl::Blower2:
    case DockControl::Blower3:
    case DockControl::Blower4:
      h.blower = static_cast<vehicle::HvacBlower>(
          static_cast<std::uint8_t>(static_cast<int>(c) - static_cast<int>(DockControl::BlowerOff)));
      if (h.blower == vehicle::HvacBlower::Off) {
        h.ac_enabled = false;
      }
      break;
    case DockControl::Ac:
      h.ac_enabled = !h.ac_enabled;
      break;
    case DockControl::ModeVent:
      h.mode = vehicle::HvacMode::Vent;
      break;
    case DockControl::ModeFloor:
      h.mode = vehicle::HvacMode::Floor;
      break;
    case DockControl::ModeDefrost:
      h.mode = vehicle::HvacMode::Defrost;
      break;
    // A lamp command carries the whole group, so it is refused while any reported member is
    // unknown: it would overwrite MCU state with a guess.
    case DockControl::Headlamps:
      if (!doors_known_) {
        return;
      }
      l.headlights_on = !l.headlights_on;
      if (l.headlights_on) {
        l.lamp_doors_open = true;
      } else {
        l.highbeams_on = false;
      }
      break;
    case DockControl::Doors:
      if (!doors_known_) {
        return;
      }
      l.lamp_doors_open = !l.lamp_doors_open;
      if (!l.lamp_doors_open) {
        l.headlights_on = false;
        l.highbeams_on = false;
      }
      break;
    case DockControl::HighBeams:
      if (!doors_known_ || !high_known_) {
        return;
      }
      l.highbeams_on = !l.highbeams_on;
      if (l.highbeams_on) {
        l.headlights_on = true;
        l.lamp_doors_open = true;
      }
      break;
    case DockControl::Count:
      return;
  }

  // A control that changes nothing (the mode already selected) sends nothing.
  std::optional<vehicle::SubmitResult> result;
  const auto kind = h != hvac_ ? vehicle::CommandKind::Hvac : vehicle::CommandKind::Lamp;

  if (h != hvac_) {
    result = sink_.send_hvac(h);
  } else if (l != lamps_) {
    result = sink_.send_lamp(l);
  }

  if (!result) {
    return;
  }

  // Accepted: the status now follows the sink's. Busy: keep showing the command in flight.

  switch (*result) {
    case vehicle::SubmitResult::Accepted:
      submitted_ = kind;
      break;
    case vehicle::SubmitResult::Busy:
      break;
    case vehicle::SubmitResult::Unavailable:
      submitted_.reset();
      last_status_ = vehicle::CommandStatus::Unavailable;
      break;
    case vehicle::SubmitResult::Invalid:
      submitted_.reset();
      last_status_ = vehicle::CommandStatus::Rejected;
      break;
  }
}

}  // namespace c3::app
