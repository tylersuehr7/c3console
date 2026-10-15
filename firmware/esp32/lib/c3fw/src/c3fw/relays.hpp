// Relay and solenoid drive derived from the commanded state (spec §6.1, §7).
//
// Blower: one of four relays selects a resistor-pack tap; speed 0 energises none. The A/C
// clutch is one relay. Mode is two vacuum solenoids: vent releases both, floor and defrost
// energise one each. Lamps: one relay in PARALLEL with the factory switch (safety invariant
// 3; the switch stays authoritative), one across the dimmer's high-beam output under the
// same rule, and one for the headlight doors. Defrost also has a hard-wired path outside this
// firmware (spec §6.3).
//
// Hold-last-state (spec §4.3): nothing here times out. The outputs change only on a command,
// so a Pi that goes quiet leaves the cabin exactly as it was.
#pragma once

#include "telemetry/frame.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace c3fw {

/// Relay board outputs, in pin-table order (include/pins.hpp).
enum class Output : std::uint8_t {
  Blower1,
  Blower2,
  Blower3,
  Blower4,
  AcClutch,
  FloorSolenoid,
  DefrostSolenoid,
  HeadlampRelay,  // parallel to the factory switch, never in series
  HighBeamRelay,  // parallel to the dimmer's high-beam output, same rule
  DoorRelay,
  Count,
};
constexpr std::size_t kOutputCount = static_cast<std::size_t>(Output::Count);
constexpr std::size_t kBlowerTapCount = 4;  // Output::Blower1 .. Blower4 lead the table

/// The last accepted command, which is also what every frame re-asserts.
struct CommandedState {
  c3::vehicle::RelayState relays{};  // blower, ac, mode, headlights
  bool doors = false;
  bool high = false;
};

using Outputs = std::array<bool, kOutputCount>;

/// Energised outputs for a commanded state. The command parser already refuses A/C without
/// airflow; the check here is the last line, so a bad state can never engage the clutch.
[[nodiscard]] constexpr Outputs outputs_for(const CommandedState& s) noexcept {
  using c3::vehicle::HvacBlower;
  using c3::vehicle::HvacMode;
  Outputs out{};

  const auto set = [&](Output o, bool on) noexcept { out[static_cast<std::size_t>(o)] = on; };

  set(Output::Blower1, s.relays.blower == HvacBlower::L1);
  set(Output::Blower2, s.relays.blower == HvacBlower::L2);
  set(Output::Blower3, s.relays.blower == HvacBlower::L3);
  set(Output::Blower4, s.relays.blower == HvacBlower::L4);
  set(Output::AcClutch, s.relays.ac_enabled && s.relays.blower > HvacBlower::Off);
  set(Output::FloorSolenoid, s.relays.mode == HvacMode::Floor);
  set(Output::DefrostSolenoid, s.relays.mode == HvacMode::Defrost);
  set(Output::HeadlampRelay, s.relays.headlights_on);
  set(Output::HighBeamRelay, s.high);
  set(Output::DoorRelay, s.doors);

  return out;
}

}  // namespace c3fw
