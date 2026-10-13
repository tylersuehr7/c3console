// MockCommandSink — the command path for the mock telemetry source (desktop and headless runs).
//
// Logs each command line exactly as it would go over UART and feeds the electrical loads to
// the MockSource, whose next frame re-asserts them, so the dock round-trips the way it will
// against the real MCU. Validation matches UartSource: an invalid state is refused before it
// is logged or applied. Loads accumulate: an HVAC command leaves the lamp loads as they were.
#pragma once

#include "telemetry/mock_source.hpp"
#include "vehicle/vehicle_commands.hpp"

namespace c3::app {

class MockCommandSink final : public vehicle::CommandSink {
public:
  /// `mock` may be null for replay: every command is then Unavailable. The source must outlive
  /// the sink.
  explicit MockCommandSink(telemetry::MockSource* mock) noexcept : mock_(mock) {}

  /// @return Unavailable with no source, Invalid for a state is_valid() rejects, else Accepted
  [[nodiscard]] vehicle::SubmitResult send_hvac(const vehicle::HvacState& s) noexcept override;
  [[nodiscard]] vehicle::SubmitResult send_lamp(const vehicle::LampState& s) noexcept override;

  /// The mock applies a command on its next frame, so a command is always Confirmed.
  [[nodiscard]] vehicle::CommandStatus status(vehicle::CommandKind /*kind*/) const noexcept override {
    return mock_ != nullptr ? vehicle::CommandStatus::Confirmed : vehicle::CommandStatus::Unavailable;
  }

private:
  telemetry::MockSource* mock_;
  telemetry::MockLoads loads_{};  // the last accepted HVAC and lamp states, merged
};

}  // namespace c3::app
