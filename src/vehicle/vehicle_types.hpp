#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace c3::vehicle {

/// C3 Corvette HVAC has very limited modes.
enum class HvacMode : uint8_t { Vent = 0, Floor, Defrost };

/// C3 Corvette blower only has Off or Level 1 / Level 2 / Level 3 / Level 4.
enum class HvacBlower : uint8_t { Off = 0, L1, L2, L3, L4 };

[[nodiscard]] constexpr std::string_view to_string(HvacMode mode) noexcept {
  switch (mode) {
    case HvacMode::Vent:
      return "vent";
    case HvacMode::Floor:
      return "floor";
    case HvacMode::Defrost:
      return "defrost";
  }
  return "vent";
}

/// Programmable HVAC controls in the C3 Corvette.
struct HvacState {
  HvacMode mode{HvacMode::Vent};
  HvacBlower blower{HvacBlower::Off};
  bool ac_enabled{false};

  [[nodiscard]] constexpr bool operator==(const HvacState&) const noexcept = default;
};

/// Validates the invariants of HVAC state.
///
/// Invariants:
/// - Blower must be on when A/C is on
///
/// @return true if state is valid; otherwise false
[[nodiscard]] constexpr bool is_valid(const HvacState& state) noexcept {
  return (!state.ac_enabled || state.blower > HvacBlower::Off);
}

/// Programmable Lamp (headlights) controls in the C3 Corvette.
struct LampState {
  bool lamp_doors_open{false};  // C3 has retractable lamp doors (they popup open and closed)
  bool headlights_on{false};    // parallel relay; factor switch stays authoritative
  bool highbeams_on{false};     // a second parallel relay across the dimmer output

  [[nodiscard]] constexpr bool operator==(const LampState&) const noexcept = default;
};

/// Validates the invariants of Lamp state.
///
/// Invariants:
/// - High beams can only be on if headlights are on (same light source).
/// - Headlights can only be on if lamp doors are opened.
///
/// @return true if state is valid; otherwise false
[[nodiscard]] constexpr bool is_valid(const LampState& state) noexcept {
  if (state.highbeams_on && !state.headlights_on) {
    return false;
  } else if (state.headlights_on && !state.lamp_doors_open) {
    return false;
  }
  return true;
}

/// Relay feedback describes *parallel outputs*, never the factory switch.
struct RelayState {
  HvacBlower blower{HvacBlower::Off};
  HvacMode mode{HvacMode::Vent};
  bool ac_enabled{false};
  bool headlights_on{false};            // parallel relay only; the factory switch stays authoritative
  std::optional<bool> lamp_doors_open;  // lamp doors relay; absent from firmware that predates it
  std::optional<bool> highbeams_on;     // high-beam relay; absent from firmware that predates it
};

}  // namespace c3::vehicle