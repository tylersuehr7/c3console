// Render loop — the console's main loop: ingest telemetry, route touch, update the scene,
// draw, present, repeat at 60 Hz. One tick is one frame. Blocking work (recording, logging,
// settings writes) is handed to BackgroundIO; the tick itself never allocates and blocks on
// nothing but the display's present.
#pragma once

#include "app/dock_controller.hpp"
#include "app/well_controller.hpp"
#include "platform/debug_frame_capture.hpp"
#include "platform/display_backend.hpp"
#include "platform/input_source.hpp"
#include "platform/watchdog.hpp"
#include "render/console_scene.hpp"
#include "telemetry/csv_recorder.hpp"
#include "telemetry/sensor_source.hpp"

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>

namespace c3::app {

struct RenderLoopOptions {
  /// When set: run for `screenshot_after_s` seconds, capture the next frame to this PPM
  /// path via `capture`, and return. Verification aid; not used in the car.
  std::optional<std::string> screenshot_path;
  float screenshot_after_s = 0.5F;
  platform::DebugFrameCapture* capture = nullptr;  // may be null when no screenshot is requested
  std::uint32_t stall_timeout_ms = 1000;           // gauges grey out past this (safety invariant 4)
  platform::Watchdog* watchdog = nullptr;          // null on the desktop; kicked after each presented frame
  telemetry::CsvRecorder* recorder = nullptr;      // set by --record
  std::string settings_path;                       // where committed settings changes are written; empty = nowhere
};

/// Runs until `stop` is set, the input source requests quit, or the screenshot is taken.
///
/// Each tick: drain the source and feed the recorder, adopt the newest frame, update the dock
/// and well controllers, route and dispatch touch, draw, present. Touches that belong to
/// CarPlay are logged at debug level until the dongle bridge exists. All objects passed in
/// must outlive the call.
///
/// @return 0 on a clean exit, 1 if frame acquisition or presentation fails
[[nodiscard]] int run_render_loop(platform::DisplayBackend& display, platform::InputSource& input,
                                  telemetry::SensorSource& source, render::ConsoleScene& scene,
                                  DockController& controls, WellController& surfaces, const RenderLoopOptions& opts,
                                  const std::atomic<bool>& stop);

}  // namespace c3::app
