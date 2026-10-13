// Display unit setting and the gauge scales it selects.
//
// Telemetry stays in the MCU's units (°F, psi); conversion happens at draw time.
// The red zones move with the scale so the picture keeps agreeing with the MCU's
// fault thresholds (spec §4.1); nothing here decides a fault.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace c3::render {

enum class TempUnit : std::uint8_t { Fahrenheit, Celsius };
constexpr std::array<TempUnit, 2> kAllTempUnits = {TempUnit::Fahrenheit, TempUnit::Celsius};

enum class PressureUnit : std::uint8_t { Psi, Bar };
constexpr std::array<PressureUnit, 2> kAllPressureUnits = {PressureUnit::Psi, PressureUnit::Bar};

/// Config / settings spelling: "F" | "C", "psi" | "bar".
[[nodiscard]] constexpr std::string_view to_string(TempUnit u) noexcept {
  return u == TempUnit::Celsius ? "C" : "F";
}
[[nodiscard]] constexpr std::string_view to_string(PressureUnit u) noexcept {
  return u == PressureUnit::Bar ? "bar" : "psi";
}

struct Units {
  TempUnit temp = TempUnit::Fahrenheit;
  PressureUnit pressure = PressureUnit::Psi;

  [[nodiscard]] constexpr bool operator==(const Units&) const noexcept = default;
};

/// Converts the MCU's °F to the display unit.
[[nodiscard]] constexpr float to_display_temp(TempUnit u, float deg_f) noexcept {
  return u == TempUnit::Celsius ? (deg_f - 32.0F) * 5.0F / 9.0F : deg_f;
}
/// Converts the MCU's psi to the display unit.
[[nodiscard]] constexpr float to_display_pressure(PressureUnit u, float psi) noexcept {
  return u == PressureUnit::Bar ? psi * 0.0689476F : psi;
}

/// Arc-gauge scale for one unit choice.
struct UnitScale {
  std::string_view unit;  // suffix drawn under the numeral and in banners
  float min;
  float max;
  float major;  // tick step
  int decimals;
  float zone_lo;  // red band, in display units
  float zone_hi;
};

/// Coolant arc: 100–260 °F with the red zone from 235, or 40–120 °C from 113 (mockup).
[[nodiscard]] constexpr UnitScale temp_scale(TempUnit u) noexcept {
  if (u == TempUnit::Celsius) {
    return {.unit =
                "\xC2\xB0"
                "C",
            .min = 40.0F,
            .max = 120.0F,
            .major = 20.0F,
            .decimals = 0,
            .zone_lo = 113.0F,
            .zone_hi = 120.0F};
  }
  return {.unit =
              "\xC2\xB0"
              "F",
          .min = 100.0F,
          .max = 260.0F,
          .major = 40.0F,
          .decimals = 0,
          .zone_lo = 235.0F,
          .zone_hi = 260.0F};
}

/// Oil arc: 0–80 psi with the low zone to 12, or 0–6 bar to 0.8 (mockup).
[[nodiscard]] constexpr UnitScale pressure_scale(PressureUnit u) noexcept {
  if (u == PressureUnit::Bar) {
    return {.unit = "bar", .min = 0.0F, .max = 6.0F, .major = 1.5F, .decimals = 1, .zone_lo = 0.0F, .zone_hi = 0.8F};
  }
  return {.unit = "psi", .min = 0.0F, .max = 80.0F, .major = 20.0F, .decimals = 0, .zone_lo = 0.0F, .zone_hi = 12.0F};
}

}  // namespace c3::render
