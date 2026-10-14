// Headless run (--headless): the telemetry path without a display. Development aid for the
// bench: proves a source delivers frames, records a drive, and logs one summary line per
// second. Uses the same ingestor, event loop and background worker as the render loop.
#pragma once

#include "telemetry/csv_recorder.hpp"
#include "telemetry/sensor_source.hpp"

#include <atomic>
#include <cstdint>

namespace c3::app {

/// Runs until `seconds` elapse (0 = until `stop` is set) or `stop` becomes true. Polls the
/// source at the MCU's 10 Hz, or on its descriptor when it has one. Every accepted frame goes
/// to `recorder` when one is given. Logs a summary line each second and totals at the end.
///
/// @return 0, or 1 if the source never produced a frame (the bench check failed)
[[nodiscard]] int run_headless(telemetry::SensorSource& source, std::uint32_t seconds, const std::atomic<bool>& stop,
                               telemetry::CsvRecorder* recorder = nullptr);

}  // namespace c3::app
