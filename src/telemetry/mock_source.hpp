// Synthetic telemetry from a small physics model.
//
// Models scenarios, doesn't randomize. The output is deterministic for a given profile,
// clock and load inputs, so the UI can be exercised against known behavior.
//
// Fixed step at 10Hz like the real MCU. poll() catches up on elapsed ticks and returns
// the latest frame; it never allocates or throws.
#pragma once

#include "frame.hpp"
#include "sensor_source.hpp"
#include "utils/clock.hpp"
#include "vehicle/vehicle_types.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace c3::telemetry {

enum class MockProfile : std::uint8_t {
  ColdStart,
  WarmupDrive,
  HotIdle,
  HighwayCruise,
  OilPressureLoss,
  Overheat,
  SenderOpen,  // oil pressure sender open-circuit
  AlternatorFail,
  LinkLoss,  // WarmupDrive for 10s, then no frames: the UART died (invariant 4 test)
  Count
};

[[nodiscard]] constexpr std::string_view to_string(MockProfile p) noexcept {
  switch (p) {
    case MockProfile::ColdStart:
      return "ColdStart";
    case MockProfile::WarmupDrive:
      return "WarmupDrive";
    case MockProfile::HotIdle:
      return "HotIdle";
    case MockProfile::HighwayCruise:
      return "HighwayCruise";
    case MockProfile::OilPressureLoss:
      return "OilPressureLoss";
    case MockProfile::Overheat:
      return "Overheat";
    case MockProfile::SenderOpen:
      return "SenderOpen";
    case MockProfile::AlternatorFail:
      return "AlternatorFail";
    case MockProfile::LinkLoss:
      return "LinkLoss";
    case MockProfile::Count:
      break;
  }
  return "?";
}

/// Case-sensitive. nullopt if unknown.
[[nodiscard]] std::optional<MockProfile> parse_mock_profile(std::string_view text) noexcept;

/// Electrical loads that sag the charging voltage (spec §9.1: blower and headlights).
struct MockLoads {
  vehicle::HvacBlower blower = vehicle::HvacBlower::Off;
  vehicle::HvacMode mode = vehicle::HvacMode::Vent;
  bool ac = false;
  bool headlights = false;
  bool doors = false;
  bool high = false;
};

// Simulation only. The production Pi renderer consumes fault codes from the MCU.
class MockFaults {
public:
  void apply(Frame& frame, bool oil_sender_open = false) noexcept;

private:
  std::array<bool, 5> active_{};
};

class MockSource final : public SensorSource {
public:
  static constexpr std::uint32_t kPeriodMs = 100;        // 10Hz, spec §4
  static constexpr std::uint32_t kMaxCatchUpTicks = 50;  // bound work after a long pause
  static constexpr double kLinkLossAfterS = 10.0;        // LinkLoss profile goes silent here

  MockSource(MockProfile profile, utils::MonotonicClock clock) noexcept;

  [[nodiscard]] std::optional<Frame> poll() noexcept override;
  [[nodiscard]] SourceHealth health() const noexcept override;

  void set_loads(MockLoads loads) noexcept { loads_ = loads; }
  [[nodiscard]] MockProfile profile() const noexcept { return profile_; }

private:
  void step(double dt_s) noexcept;
  [[nodiscard]] Frame make_frame() noexcept;

  MockProfile profile_;
  utils::MonotonicClock clock_;
  MockLoads loads_{};

  bool started_ = false;
  std::uint64_t start_ms_ = 0;
  std::uint64_t ticks_ = 0;  // 100ms ticks stepped so far

  // Model state. Initial values from the reference: engine just started, cold.
  double t_s_ = 0.0;
  double rpm_ = 800.0;
  double coolant_f_ = 72.0;
  double oil_psi_ = 0.0;
  double fuel_fraction_ = 0.62;
  double voltage_v_ = 14.2;

  MockFaults faults_;
  SourceHealth health_{};
};

}  // namespace c3::telemetry
