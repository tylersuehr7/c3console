// Sensor source factory — turns the resolved Config into the one SensorSource the console runs
// on: the mock model, a recorded drive, or the MCU over UART. Init-time: opens devices and
// files, allocates, logs. Errors are returned, not thrown, so main() can print and exit.
#pragma once

#include "config.hpp"
#include "telemetry/mock_source.hpp"
#include "telemetry/sensor_source.hpp"
#include "telemetry/uart_source.hpp"
#include "utils/clock.hpp"
#include "vehicle/vehicle_commands.hpp"

#include <memory>
#include <string>

namespace c3::app {

struct SourceResult {
  std::unique_ptr<telemetry::SensorSource> source;  // null when `error` is set
  telemetry::MockSource* mock = nullptr;            // non-owning view of `source` when it is a mock
  std::string error;                                // why `source` is null; prefixed with the source kind
  // The command path to the MCU, when the source has one: the UartSource itself. Null for the
  // mock (build a MockCommandSink on `mock`) and for replay (no MCU to command).
  vehicle::CommandSink* commands = nullptr;
};

/// Opens the source `cfg.source` names, with `cfg`'s device, file, profile and stall timeout.
/// Replay warnings and its summary go to the log. `clock` is the monotonic clock the source
/// times frames with; tests inject a fake one.
[[nodiscard]] SourceResult make_sensor_source(const Config& cfg, utils::MonotonicClock clock = utils::steady_now_ms);

}  // namespace c3::app
