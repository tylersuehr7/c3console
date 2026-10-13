// dock_controller.cpp — dock taps become HVAC and lamp commands; frames become the dock model.
#include "app/dock_controller.hpp"
#include "platform/input_source.hpp"
#include "render/dock.hpp"
#include "render/layout.hpp"
#include "telemetry/frame.hpp"
#include "vehicle/vehicle_commands.hpp"
#include "vehicle/vehicle_types.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <vector>

using c3::app::DockController;
using c3::platform::TouchEvent;
using c3::platform::TouchPhase;
using c3::render::DockControl;
using c3::render::DockLayout;
using c3::render::DockModel;
using c3::render::Rect;
using c3::telemetry::Frame;
using c3::vehicle::CommandKind;
using c3::vehicle::CommandStatus;
using c3::vehicle::HvacBlower;
using c3::vehicle::HvacMode;
using c3::vehicle::HvacState;
using c3::vehicle::LampState;
using c3::vehicle::RelayState;
using c3::vehicle::SubmitResult;

CATCH_REGISTER_ENUM(DockControl, DockControl::BlowerOff, DockControl::Blower1, DockControl::Blower2,
                    DockControl::Blower3, DockControl::Blower4, DockControl::Ac, DockControl::ModeVent,
                    DockControl::ModeFloor, DockControl::ModeDefrost, DockControl::Headlamps, DockControl::Doors,
                    DockControl::HighBeams, DockControl::Count)
CATCH_REGISTER_ENUM(CommandStatus, CommandStatus::Idle, CommandStatus::Pending, CommandStatus::Acknowledged,
                    CommandStatus::Confirmed, CommandStatus::Rejected, CommandStatus::TimedOut,
                    CommandStatus::Unavailable)
CATCH_REGISTER_ENUM(HvacBlower, HvacBlower::Off, HvacBlower::L1, HvacBlower::L2, HvacBlower::L3, HvacBlower::L4)
CATCH_REGISTER_ENUM(HvacMode, HvacMode::Vent, HvacMode::Floor, HvacMode::Defrost)

namespace {

/// A CommandSink that records what it is asked and answers as told.
class RecordingSink final : public c3::vehicle::CommandSink {
public:
  std::vector<HvacState> hvac;
  std::vector<LampState> lamps;
  SubmitResult next_result = SubmitResult::Accepted;
  CommandStatus hvac_status = CommandStatus::Pending;
  CommandStatus lamp_status = CommandStatus::Acknowledged;

  [[nodiscard]] SubmitResult send_hvac(const HvacState& s) noexcept override {
    hvac.push_back(s);
    return next_result;
  }
  [[nodiscard]] SubmitResult send_lamp(const LampState& s) noexcept override {
    lamps.push_back(s);
    return next_result;
  }
  [[nodiscard]] CommandStatus status(CommandKind kind) const noexcept override {
    return kind == CommandKind::Hvac ? hvac_status : lamp_status;
  }
  [[nodiscard]] std::size_t sent() const noexcept { return hvac.size() + lamps.size(); }
};

constexpr Rect kDock{.x = 40.0F, .y = 2128.0F, .w = 1520.0F, .h = 392.0F};  // reference layout

/// A live frame whose relays report the given state. Doors and high beams are reported unless
/// `legacy` asks for a frame from firmware that predates them.
[[nodiscard]] Frame frame_with(const HvacState& h, const LampState& l, bool legacy = false) noexcept {
  Frame f;
  f.relays = RelayState{.blower = h.blower,
                        .mode = h.mode,
                        .ac_enabled = h.ac_enabled,
                        .headlights_on = l.headlights_on,
                        .lamp_doors_open = legacy ? std::nullopt : std::optional<bool>(l.lamp_doors_open),
                        .highbeams_on = legacy ? std::nullopt : std::optional<bool>(l.highbeams_on)};
  return f;
}

constexpr HvacState kHvac{.mode = HvacMode::Floor, .blower = HvacBlower::L2, .ac_enabled = true};
constexpr LampState kLampsOff{};
constexpr LampState kLampsOn{.lamp_doors_open = true, .headlights_on = true, .highbeams_on = false};

/// Controller, sink and layout, with a live frame already applied unless a test says otherwise.
struct Rig {
  RecordingSink sink;
  DockController ctl{sink};
  DockLayout dock = DockLayout::compute(kDock);

  void go_live(const HvacState& h = kHvac, const LampState& l = kLampsOff, bool legacy = false) {
    ctl.apply_frame(frame_with(h, l, legacy));
    ctl.set_live(true);
  }

  void touch(TouchPhase phase, float x, float y) {
    ctl.on_touch(TouchEvent{.phase = phase, .id = 1, .x = x, .y = y}, dock);
  }
  void touch(TouchPhase phase, DockControl c) {
    const Rect& r = dock.rect(c);
    touch(phase, r.cx(), r.cy());
  }
  void tap(DockControl c) {
    touch(TouchPhase::Down, c);
    touch(TouchPhase::Up, c);
  }
};

// ---------------------------------------------------------------------------
// model from frames
// ---------------------------------------------------------------------------

TEST_CASE("DockController: before any frame the dock is unknown and idle", "[dock_controller]") {
  const Rig rig;
  const DockModel m = rig.ctl.model();
  CHECK_FALSE(m.live);
  CHECK_FALSE(m.doors_known);
  CHECK_FALSE(m.high_known);
  CHECK_FALSE(m.pressed.has_value());
  CHECK(m.command == CommandStatus::Idle);
  CHECK(m.hvac == HvacState{});
  CHECK(m.lamps == LampState{});
}

TEST_CASE("DockController: a frame with relays populates the model; live needs both the flag and the relays",
          "[dock_controller]") {
  Rig rig;
  rig.ctl.apply_frame(frame_with(kHvac, kLampsOn));
  DockModel m = rig.ctl.model();
  CHECK_FALSE(m.live);  // relays known, link not yet declared live
  CHECK(m.hvac == kHvac);
  CHECK(m.lamps == kLampsOn);
  CHECK(m.doors_known);
  CHECK(m.high_known);

  rig.ctl.set_live(true);
  CHECK(rig.ctl.model().live);

  rig.ctl.apply_frame(Frame{});  // a frame without relays: state unknown again
  m = rig.ctl.model();
  CHECK_FALSE(m.live);
}

TEST_CASE("DockController: legacy firmware that omits doors and high beams leaves those unknown", "[dock_controller]") {
  Rig rig;
  rig.go_live(kHvac, kLampsOn, true);
  const DockModel m = rig.ctl.model();
  CHECK(m.live);
  CHECK(m.lamps.headlights_on);
  CHECK_FALSE(m.doors_known);
  CHECK_FALSE(m.high_known);
}

TEST_CASE("DockController: a later frame that omits the optional members keeps their last value", "[dock_controller]") {
  Rig rig;
  rig.go_live(kHvac, kLampsOn);
  rig.go_live(kHvac, kLampsOff, true);
  const DockModel m = rig.ctl.model();
  CHECK(m.lamps.lamp_doors_open);  // held, but flagged unknown
  CHECK_FALSE(m.doors_known);
}

// ---------------------------------------------------------------------------
// press and release
// ---------------------------------------------------------------------------

TEST_CASE("DockController: a control is pressed from Down until Up or Cancel", "[dock_controller]") {
  Rig rig;
  rig.go_live();
  rig.touch(TouchPhase::Down, DockControl::Blower3);
  CHECK(rig.ctl.model().pressed == DockControl::Blower3);
  rig.touch(TouchPhase::Move, kDock.x + 1.0F, kDock.y + 1.0F);  // off the control
  CHECK(rig.ctl.model().pressed == DockControl::Blower3);
  rig.touch(TouchPhase::Cancel, kDock.x + 1.0F, kDock.y + 1.0F);
  CHECK_FALSE(rig.ctl.model().pressed.has_value());
  CHECK(rig.sink.sent() == 0);
}

TEST_CASE("DockController: a release on the pressed control activates it; elsewhere it does not", "[dock_controller]") {
  Rig rig;
  rig.go_live();

  rig.touch(TouchPhase::Down, DockControl::Blower3);
  rig.touch(TouchPhase::Up, DockControl::Blower4);  // slid onto a neighbour
  CHECK(rig.sink.sent() == 0);
  CHECK_FALSE(rig.ctl.model().pressed.has_value());

  rig.touch(TouchPhase::Down, DockControl::Blower3);
  rig.touch(TouchPhase::Move, DockControl::Blower4);
  rig.touch(TouchPhase::Up, DockControl::Blower3);  // and back again
  REQUIRE(rig.sink.hvac.size() == 1);
  CHECK(rig.sink.hvac[0].blower == HvacBlower::L3);
}

TEST_CASE("DockController: touches on island chrome press nothing and send nothing", "[dock_controller]") {
  Rig rig;
  rig.go_live();
  rig.touch(TouchPhase::Down, rig.dock.hvac_island.x + 40.0F, kDock.y + 30.0F);  // the HVAC title
  CHECK_FALSE(rig.ctl.model().pressed.has_value());
  rig.touch(TouchPhase::Up, rig.dock.hvac_island.x + 40.0F, kDock.y + 30.0F);
  CHECK(rig.sink.sent() == 0);
  rig.touch(TouchPhase::Up, DockControl::Ac);  // an Up with no Down
  CHECK(rig.sink.sent() == 0);
}

// ---------------------------------------------------------------------------
// availability
// ---------------------------------------------------------------------------

TEST_CASE("DockController: without a live frame a tap sends nothing and reads UNAVAILABLE", "[dock_controller]") {
  Rig rig;
  rig.ctl.apply_frame(frame_with(kHvac, kLampsOn));  // relays known but the link is not live
  rig.tap(DockControl::Blower4);
  CHECK(rig.sink.sent() == 0);
  CHECK(rig.ctl.model().command == CommandStatus::Unavailable);

  Rig other;
  other.ctl.set_live(true);  // live but no relays yet
  other.tap(DockControl::Ac);
  CHECK(other.sink.sent() == 0);
  CHECK(other.ctl.model().command == CommandStatus::Unavailable);
}

TEST_CASE("DockController: the link going stale after an accepted command does not undo its status",
          "[dock_controller]") {
  Rig rig;
  rig.go_live();
  rig.tap(DockControl::Blower4);
  REQUIRE(rig.sink.hvac.size() == 1);
  rig.ctl.set_live(false);
  CHECK(rig.ctl.model().command == rig.sink.hvac_status);  // still the sink's word
  rig.tap(DockControl::Blower1);
  CHECK(rig.sink.hvac.size() == 1);
  CHECK(rig.ctl.model().command == CommandStatus::Unavailable);
}

// ---------------------------------------------------------------------------
// HVAC commands
// ---------------------------------------------------------------------------

TEST_CASE("DockController: blower buttons send the MCU's state with the blower changed", "[dock_controller]") {
  Rig rig;
  rig.go_live(kHvac);
  rig.tap(DockControl::Blower4);
  REQUIRE(rig.sink.hvac.size() == 1);
  CHECK(rig.sink.hvac[0] == HvacState{.mode = HvacMode::Floor, .blower = HvacBlower::L4, .ac_enabled = true});
  rig.tap(DockControl::Blower1);
  REQUIRE(rig.sink.hvac.size() == 2);
  CHECK(rig.sink.hvac[1].blower == HvacBlower::L1);
  CHECK(rig.sink.hvac[1].ac_enabled);  // still from the MCU's state, which has not changed
  CHECK(rig.sink.lamps.empty());
}

TEST_CASE("DockController: blower Off also drops the A/C so the command stays valid", "[dock_controller]") {
  Rig rig;
  rig.go_live(kHvac);
  rig.tap(DockControl::BlowerOff);
  REQUIRE(rig.sink.hvac.size() == 1);
  CHECK(rig.sink.hvac[0].blower == HvacBlower::Off);
  CHECK_FALSE(rig.sink.hvac[0].ac_enabled);
  CHECK(c3::vehicle::is_valid(rig.sink.hvac[0]));
}

TEST_CASE("DockController: A/C toggles against the MCU's state", "[dock_controller]") {
  Rig rig;
  rig.go_live(kHvac);  // A/C on
  rig.tap(DockControl::Ac);
  REQUIRE(rig.sink.hvac.size() == 1);
  CHECK_FALSE(rig.sink.hvac[0].ac_enabled);
  CHECK(rig.sink.hvac[0].blower == HvacBlower::L2);

  rig.go_live(HvacState{.mode = HvacMode::Vent, .blower = HvacBlower::L1, .ac_enabled = false});
  rig.tap(DockControl::Ac);
  REQUIRE(rig.sink.hvac.size() == 2);
  CHECK(rig.sink.hvac[1].ac_enabled);
}

TEST_CASE("DockController: A/C with the blower off is a request the sink refuses, not one the dock hides",
          "[dock_controller]") {
  // The controller does not raise the blower on the user's behalf; the MCU (and every sink)
  // rejects the invalid state and the dock shows REFUSED.
  Rig rig;
  rig.go_live(HvacState{});
  rig.sink.next_result = SubmitResult::Invalid;
  rig.tap(DockControl::Ac);
  REQUIRE(rig.sink.hvac.size() == 1);
  CHECK_FALSE(c3::vehicle::is_valid(rig.sink.hvac[0]));
  CHECK(rig.ctl.model().command == CommandStatus::Rejected);
}

TEST_CASE("DockController: mode buttons send the mode; the selected mode sends nothing", "[dock_controller]") {
  Rig rig;
  rig.go_live(kHvac);  // Floor
  rig.tap(DockControl::ModeFloor);
  CHECK(rig.sink.sent() == 0);
  rig.tap(DockControl::ModeDefrost);
  REQUIRE(rig.sink.hvac.size() == 1);
  CHECK(rig.sink.hvac[0].mode == HvacMode::Defrost);
  CHECK(rig.sink.hvac[0].blower == HvacBlower::L2);
  rig.tap(DockControl::ModeVent);
  REQUIRE(rig.sink.hvac.size() == 2);
  CHECK(rig.sink.hvac[1].mode == HvacMode::Vent);
}

TEST_CASE("DockController: the selected blower level sends nothing", "[dock_controller]") {
  Rig rig;
  rig.go_live(kHvac);  // L2
  rig.tap(DockControl::Blower2);
  CHECK(rig.sink.sent() == 0);
  CHECK(rig.ctl.model().command == CommandStatus::Idle);
}

// ---------------------------------------------------------------------------
// lamp commands
// ---------------------------------------------------------------------------

TEST_CASE("DockController: lights on raises the doors; lights off drops the high beams", "[dock_controller]") {
  Rig rig;
  rig.go_live(kHvac, kLampsOff);
  rig.tap(DockControl::Headlamps);
  REQUIRE(rig.sink.lamps.size() == 1);
  CHECK(rig.sink.lamps[0] == LampState{.lamp_doors_open = true, .headlights_on = true, .highbeams_on = false});
  CHECK(c3::vehicle::is_valid(rig.sink.lamps[0]));
  CHECK(rig.sink.hvac.empty());

  rig.go_live(kHvac, LampState{.lamp_doors_open = true, .headlights_on = true, .highbeams_on = true});
  rig.tap(DockControl::Headlamps);
  REQUIRE(rig.sink.lamps.size() == 2);
  CHECK(rig.sink.lamps[1] == LampState{.lamp_doors_open = true, .headlights_on = false, .highbeams_on = false});
}

TEST_CASE("DockController: closing the doors turns every lamp off; opening them changes nothing else",
          "[dock_controller]") {
  Rig rig;
  rig.go_live(kHvac, LampState{.lamp_doors_open = true, .headlights_on = true, .highbeams_on = true});
  rig.tap(DockControl::Doors);
  REQUIRE(rig.sink.lamps.size() == 1);
  CHECK(rig.sink.lamps[0] == LampState{});
  CHECK(c3::vehicle::is_valid(rig.sink.lamps[0]));

  rig.go_live(kHvac, kLampsOff);
  rig.tap(DockControl::Doors);
  REQUIRE(rig.sink.lamps.size() == 2);
  CHECK(rig.sink.lamps[1] == LampState{.lamp_doors_open = true, .headlights_on = false, .highbeams_on = false});
}

TEST_CASE("DockController: high beams bring the lights and doors with them", "[dock_controller]") {
  Rig rig;
  rig.go_live(kHvac, kLampsOff);
  rig.tap(DockControl::HighBeams);
  REQUIRE(rig.sink.lamps.size() == 1);
  CHECK(rig.sink.lamps[0] == LampState{.lamp_doors_open = true, .headlights_on = true, .highbeams_on = true});
  CHECK(c3::vehicle::is_valid(rig.sink.lamps[0]));

  rig.go_live(kHvac, LampState{.lamp_doors_open = true, .headlights_on = true, .highbeams_on = true});
  rig.tap(DockControl::HighBeams);
  REQUIRE(rig.sink.lamps.size() == 2);
  CHECK(rig.sink.lamps[1] == LampState{.lamp_doors_open = true, .headlights_on = true, .highbeams_on = false});
}

TEST_CASE("DockController: every lamp command built from a valid state is itself valid", "[dock_controller]") {
  for (const bool doors : {false, true}) {
    for (const bool lights : {false, true}) {
      for (const bool highs : {false, true}) {
        const LampState start{.lamp_doors_open = doors, .headlights_on = lights, .highbeams_on = highs};
        if (!c3::vehicle::is_valid(start)) {
          continue;
        }
        for (const DockControl c : {DockControl::Doors, DockControl::Headlamps, DockControl::HighBeams}) {
          Rig rig;
          rig.go_live(kHvac, start);
          rig.tap(c);
          REQUIRE(rig.sink.lamps.size() == 1);
          INFO("from doors=" << doors << " lights=" << lights << " highs=" << highs << " via "
                             << Catch::StringMaker<DockControl>::convert(c));
          CHECK(c3::vehicle::is_valid(rig.sink.lamps[0]));
          CHECK(rig.sink.lamps[0] != start);
        }
      }
    }
  }
}

TEST_CASE("DockController: lamp taps are dropped while the frame leaves the lamp group unknown", "[dock_controller]") {
  Rig rig;
  rig.go_live(kHvac, kLampsOn, true);  // legacy firmware: no doors, no high beams
  rig.tap(DockControl::Headlamps);
  rig.tap(DockControl::Doors);
  rig.tap(DockControl::HighBeams);
  CHECK(rig.sink.sent() == 0);
  CHECK(rig.ctl.model().command == CommandStatus::Idle);  // silently dropped, not refused
  rig.tap(DockControl::Blower4);                          // HVAC is unaffected
  CHECK(rig.sink.hvac.size() == 1);
}

// ---------------------------------------------------------------------------
// submit results and status
// ---------------------------------------------------------------------------

TEST_CASE("DockController: an accepted command's status is the sink's, per kind", "[dock_controller]") {
  Rig rig;
  rig.go_live(kHvac, kLampsOff);
  rig.sink.hvac_status = CommandStatus::Pending;
  rig.sink.lamp_status = CommandStatus::Confirmed;
  rig.tap(DockControl::Blower4);
  CHECK(rig.ctl.model().command == CommandStatus::Pending);
  rig.sink.hvac_status = CommandStatus::Confirmed;
  CHECK(rig.ctl.model().command == CommandStatus::Confirmed);  // read live, not cached
  rig.tap(DockControl::Headlamps);
  rig.sink.lamp_status = CommandStatus::TimedOut;
  CHECK(rig.ctl.model().command == CommandStatus::TimedOut);
}

TEST_CASE("DockController: Busy keeps showing the command already in flight", "[dock_controller]") {
  Rig rig;
  rig.go_live(kHvac);
  rig.tap(DockControl::Blower4);
  rig.sink.next_result = SubmitResult::Busy;
  rig.tap(DockControl::Blower1);
  CHECK(rig.sink.hvac.size() == 2);  // the attempt was made
  CHECK(rig.ctl.model().command == rig.sink.hvac_status);
}

TEST_CASE("DockController: Unavailable and Invalid replace the status with their own", "[dock_controller]") {
  Rig rig;
  rig.go_live(kHvac);
  rig.sink.next_result = SubmitResult::Unavailable;
  rig.tap(DockControl::Blower4);
  CHECK(rig.ctl.model().command == CommandStatus::Unavailable);
  rig.sink.next_result = SubmitResult::Invalid;
  rig.tap(DockControl::Blower1);
  CHECK(rig.ctl.model().command == CommandStatus::Rejected);
  rig.sink.next_result = SubmitResult::Accepted;
  rig.tap(DockControl::Blower3);
  CHECK(rig.ctl.model().command == rig.sink.hvac_status);
}

TEST_CASE("DockController: the model never reflects a command optimistically", "[dock_controller]") {
  Rig rig;
  rig.go_live(kHvac);
  rig.tap(DockControl::Blower4);
  CHECK(rig.ctl.model().hvac == kHvac);  // still the MCU's state until a frame says otherwise
  rig.go_live(HvacState{.mode = HvacMode::Floor, .blower = HvacBlower::L4, .ac_enabled = true});
  CHECK(rig.ctl.model().hvac.blower == HvacBlower::L4);
}

}  // namespace
