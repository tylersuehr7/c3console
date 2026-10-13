#pragma once

#include "utils/fixed_vector.hpp"
#include "vehicle/vehicle_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace c3::telemetry {

/// Sensors tapped into by the MCU, in ADC assignemtn order.
enum class Channel : std::uint8_t { Fuel = 0, Oil, Coolant, Volt, Count };
constexpr std::size_t kChannelCount = static_cast<std::size_t>(Channel::Count);

[[nodiscard]] constexpr std::string_view to_string(Channel channel) noexcept {
  switch (channel) {
    case Channel::Fuel:
      return "FUEL";
    case Channel::Oil:
      return "OIL";
    case Channel::Coolant:
      return "CLT";
    case Channel::Volt:
      return "VOLT";
    case Channel::Count:
      break;
  }
  return "?";
}

/// Fault codes emited by the MCU (`faults[]`).
enum class FaultKind : std::uint8_t {
  OilLow = 0,   // OIL_LOW
  CltHigh,      // CLT_HIGH
  FuelLow,      // FUEL_LOW
  VoltLow,      // VOLT_LOW
  VoltHigh,     // VOLT_HIGH
  SenderOpen,   // SENDER_OPEN_<ch>
  SenderShort,  // SENDER_SHORT_<ch>
  Count
};
constexpr std::size_t kFaultKindCount = static_cast<std::size_t>(FaultKind::Count);

[[nodiscard]] constexpr std::string_view to_string(FaultKind kind) noexcept {
  switch (kind) {
    case FaultKind::OilLow:
      return "OIL_LOW";
    case FaultKind::CltHigh:
      return "CLT_HIGH";
    case FaultKind::FuelLow:
      return "FUEL_LOW";
    case FaultKind::VoltLow:
      return "VOLT_LOW";
    case FaultKind::VoltHigh:
      return "VOLT_HIGH";
    case FaultKind::SenderOpen:
      return "SENDER_OPEN";
    case FaultKind::SenderShort:
      return "SENDER_SHORT";
    case FaultKind::Count:
      break;
  }
  return "?";
}

struct Fault {
  FaultKind kind{};
  Channel channel{};  // meaningful for SenderOpen / SenderShort only
};

constexpr std::size_t kMaxFaults = kFaultKindCount + 1;

struct Frame {
  std::uint32_t t_ms = 0;      // MCU milliseconds since boot
  float fuel_fraction = 0.0F;  // fraction 0..1
  float oil_psi = 0.0F;
  float coolant_f = 0.0F;  // °F
  float voltage_v = 0.0F;
  std::array<std::int32_t, kChannelCount> raw{};  // ADC counts, calibration screen only
  utils::FixedVector<Fault, kMaxFaults> faults{};
  std::optional<vehicle::RelayState> relays{};
  bool reverse = false;  // `rev`: reverse gear sensed on the lamp circuit; drives the camera
};

// The snapshot publisher copies whole Frames across threads; keep that a memcpy.
static_assert(std::is_trivially_copyable_v<Frame>);

}  // namespace c3::telemetry
