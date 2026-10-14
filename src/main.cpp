#include "app/args.hpp"
#include "app/config.hpp"
#include "app/dock_controller.hpp"
#include "app/headless.hpp"
#include "app/mock_command_sink.hpp"
#include "app/render_loop.hpp"
#include "app/sensor_source_factory.hpp"
#include "app/settings_store.hpp"
#include "app/well_controller.hpp"
#include "logging/boot_message.hpp"
#include "logging/log.hpp"
#include "platform/debug_frame_capture.hpp"
#include "platform/display_backend.hpp"
#include "platform/file_watchdog.hpp"
#include "platform/input_source.hpp"
#include "platform/panel_rotation.hpp"
#include "render/console_scene.hpp"
#include "render/fonts.hpp"
#include "telemetry/calibration.hpp"
#include "telemetry/csv_recorder.hpp"

#if defined(C3_BACKEND_DESKTOP)
#include "platform/desktop_display_backend.hpp"
#include "platform/sdl_input_source.hpp"
#elif defined(C3_BACKEND_DRM)
#include "platform/drm_display_backend.hpp"
#include "platform/libinput_input_source.hpp"
#else
#error "define C3_BACKEND_DESKTOP or C3_BACKEND_DRM"
#endif

#include <atomic>
#include <csignal>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {
constexpr int kExitOk = 0;
constexpr int kExitConfig = 1;
constexpr int kExitUsage = 2;
constexpr int kExitRuntime = 3;

#if defined(C3_BACKEND_DRM)
constexpr c3::app::Backend kBuiltBackend = c3::app::Backend::Drm;
#else
constexpr c3::app::Backend kBuiltBackend = c3::app::Backend::Desktop;
#endif

// Set from the signal handler, so it must be lock-free to be async-signal-safe.
std::atomic<bool> g_stop{false};  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
static_assert(std::atomic<bool>::is_always_lock_free);

extern "C" void on_signal(int /*signum*/) {
  g_stop.store(true, std::memory_order_relaxed);
}
}  // namespace

namespace {
// Everything after the display exists: fonts, settings, watchdog, scene, controls, loop.
// Shared by both backends so the target runs exactly the code the desktop was developed on.
int run_console(c3::platform::DisplayBackend& display, c3::platform::DebugFrameCapture* capture,
                c3::platform::InputSource& input, const c3::app::Config& cfg, const std::string& argv0,
                const c3::telemetry::CalibrationTable& calibration, const c3::app::SourceResult& src,
                c3::telemetry::CsvRecorder* recorder) {
  const std::string assets = c3::app::find_project_path(cfg.assets_dir, argv0);
  const c3::render::FontsResult fonts = c3::render::load_fonts(display.context(), assets + "/fonts");

  if (!fonts.fonts) {
    c3::logging::error("app", "fonts: ", fonts.error, " (assets_dir='", cfg.assets_dir, "')");
    return kExitRuntime;
  }

  // User settings: console.toml supplies the defaults, the data-partition file overrides them.
  c3::render::Settings initial_settings = cfg.ui;
  if (!cfg.settings_file.empty()) {
    const c3::app::SettingsLoad stored = c3::app::load_settings(cfg.settings_file, cfg.ui);
    initial_settings = stored.settings;
    switch (stored.status) {
      case c3::app::SettingsStatus::Loaded:
        c3::logging::info("app", "settings loaded from ", cfg.settings_file);
        break;
      case c3::app::SettingsStatus::Missing:
        c3::logging::info("app", "settings: ", stored.detail, "; using console.toml defaults");
        break;
      case c3::app::SettingsStatus::Corrupt:
        c3::logging::warn("app", "settings file rejected (", stored.detail,
                          "); using console.toml defaults, file kept for inspection");
        break;
    }
  }

  std::unique_ptr<c3::platform::FileWatchdog> watchdog;
  if (!cfg.watchdog_device.empty()) {
    c3::platform::FileWatchdog::Result opened = c3::platform::FileWatchdog::open(cfg.watchdog_device);
    if (!opened.watchdog) {
      c3::logging::error("app", opened.error, "; running without a watchdog");
    } else {
      watchdog = std::move(opened.watchdog);
      c3::logging::info("app", "watchdog ", cfg.watchdog_device, " kicked once per rendered frame");
    }
  }

  const c3::render::Layout layout = c3::render::Layout::compute(display.size());
  c3::app::LogAudioSink audio_sink;  // the DSP stage lands with milestone 15
  c3::app::WellController surfaces(layout, initial_settings, &calibration, audio_sink);
  if (cfg.open_surface && !surfaces.open_surface(*cfg.open_surface)) {
    c3::logging::warn("app", "--open: unknown surface '", *cfg.open_surface, "'");
  }

  c3::render::ConsoleScene scene(layout, surfaces.theme(), *fonts.fonts);
  c3::app::MockCommandSink link(src.mock);  // logs commands; echoes loads into the mock when there is one
  c3::app::DockController controls(src.commands != nullptr ? *src.commands : link);

  const c3::app::RenderLoopOptions loop_opts{
      .screenshot_path = cfg.screenshot_path,
      .screenshot_after_s = cfg.screenshot_after_s,
      .capture = capture,
      .stall_timeout_ms = static_cast<std::uint32_t>(cfg.stall_timeout_ms),
      .watchdog = watchdog.get(),
      .recorder = recorder,
      .settings_path = cfg.settings_file,
  };

  const int result =
      c3::app::run_render_loop(display, input, *src.source, scene, controls, surfaces, loop_opts, g_stop);
  if (result == 0 && watchdog) {
    watchdog->allow_clean_exit();
  }

  return result == 0 ? kExitOk : kExitRuntime;
}
}  // namespace

int main(const int argc, const char** argv) {
  c3::logging::boot_message();

  // Parse the command-line flags. argv[0] is the usual program name; a launcher may pass none.
  const std::vector<std::string> list_of_args(argv, std::next(argv, argc));
  const c3::app::ArgsResult parsed_args = c3::app::parse_args(list_of_args);
  const std::string program_name = list_of_args.empty() ? std::string("c3console") : list_of_args[0];
  if (parsed_args.error) {
    std::cerr << "c3console: " << *parsed_args.error << "\n\n" << c3::app::usage(program_name);
    return kExitUsage;
  } else if (parsed_args.args.help) {
    std::cout << c3::app::usage(program_name);
    return kExitOk;
  }

  // Read / parse the runtime configurations (TOML file)
  const c3::app::ConfigResolution resolved =
      c3::app::resolve_config(parsed_args.args, c3::app::find_config_path(parsed_args.args.config, program_name));
  switch (resolved.status) {
    case c3::app::ResolveStatus::UsageError:
      std::cerr << "c3console: " << resolved.error << "\n\n" << c3::app::usage(program_name);
      return kExitUsage;
    case c3::app::ResolveStatus::ConfigError:
      std::cerr << "c3console: " << resolved.error << "\n\n" << c3::app::usage(program_name);
      return kExitConfig;
    case c3::app::ResolveStatus::Ok:
      break;
  }
  const c3::app::Config& config = resolved.config;

  c3::logging::set_level(config.log_level);

  for (const std::string& warning : resolved.warnings) {
    c3::logging::warn("app", warning);
  }

  if (config.backend != kBuiltBackend) {
    c3::logging::warn("app", "binary was built with C3_BACKEND=", c3::app::to_string(kBuiltBackend),
                      " but config requests '", c3::app::to_string(config.backend),
                      "'; the built backend will be used");
  }

  c3::logging::info("app", "resolved configuration:\n", c3::app::describe(config));

  // Safety invariant 5: the table is read from the file, validated, and its placeholder
  // status is logged at every start. Nothing in the binary knows a sender curve.
  const c3::telemetry::CalibrationLoadResult calibration =
      c3::telemetry::load_calibration(c3::app::find_project_path(config.calibration_file, program_name));
  if (!calibration.table) {
    c3::logging::error("app", calibration.error);
    return kExitConfig;
  }
  if (calibration.table->placeholder) {
    c3::logging::warn("app", "calibration: ", c3::telemetry::describe(*calibration.table));
  } else {
    c3::logging::info("app", "calibration: ", c3::telemetry::describe(*calibration.table));
  }

  c3::app::SourceResult src = c3::app::make_sensor_source(config);
  if (!src.source) {
    c3::logging::error("app", "source: ", src.error);
    return kExitConfig;
  }
  static_cast<void>(std::signal(SIGINT, on_signal));
  static_cast<void>(std::signal(SIGTERM, on_signal));

  std::unique_ptr<c3::telemetry::CsvRecorder> recorder;
  if (!config.record_file.empty()) {
    c3::telemetry::CsvRecorder::Result opened = c3::telemetry::CsvRecorder::open(config.record_file);
    if (!opened.recorder) {
      c3::logging::error("app", opened.error);
      return kExitConfig;
    }
    recorder = std::move(opened.recorder);
    c3::logging::info("app", "recording every accepted frame to ", config.record_file, " (spec §9.3)");
  }

  if (config.headless_seconds) {
    const int result = c3::app::run_headless(*src.source, *config.headless_seconds, g_stop, recorder.get());
    return result == 0 ? kExitOk : kExitRuntime;  // 1 from run_headless means "no frame", not a config fault
  }

  const c3::platform::Size panel{.width = config.width, .height = config.height};

#if defined(C3_BACKEND_DESKTOP)
  auto display = c3::platform::DesktopDisplayBackend::create({.panel = panel, .dpi = config.dpi});

  if (!display.backend) {
    c3::logging::error("app", "display: ", display.error);
    return kExitRuntime;
  }

  c3::platform::SdlInputSource input(*display.backend);
  return run_console(*display.backend, display.backend.get(), input, config, program_name, *calibration.table, src,
                     recorder.get());
#else
  // The panel scans out in its native orientation; config says how it is mounted. The mode
  // is chosen to match the configured panel size in either orientation.
  const c3::platform::Rotation rotation =
      c3::platform::rotation_from_degrees(config.rotation).value_or(c3::platform::Rotation::R0);

  auto display = c3::platform::DrmDisplayBackend::create(
      {.device = config.drm_device, .rotation = rotation, .fallback_dpi = config.dpi, .prefer_mode = panel});

  if (!display.backend) {
    c3::logging::error("app", "display: ", display.error);
    return kExitRuntime;
  }

  if (display.backend->size().width != panel.width || display.backend->size().height != panel.height) {
    c3::logging::warn("app", "panel mode is ", display.backend->size().width, "x", display.backend->size().height,
                      " but config says ", panel.width, "x", panel.height, "; layout follows the mode");
  }

  auto input = c3::platform::LibinputInputSource::create(
      {.native = display.backend->native_size(), .rotation = rotation, .seat = "seat0"});

  if (!input.source) {
    c3::logging::error("app", input.error);
    return kExitRuntime;
  }

  return run_console(*display.backend, display.backend.get(), *input.source, config, program_name, *calibration.table,
                     src, recorder.get());
#endif
}
