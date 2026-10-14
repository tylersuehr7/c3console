// Runtime configuration: defaults <- config/console.toml <- CLI flags.
//
// Config is a plain aggregate on purpose: every field is independently valid, the
// cross-field rules live in validate(), and designated initialisers make test fixtures
// one-liners. resolve_config() is the single place the overlay order is applied.
#pragma once

#include "args.hpp"
#include "logging/log.hpp"
#include "render/settings.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace c3::app {

enum class Backend { Desktop, Drm };
constexpr std::array<Backend, 2> kAllBackends = {Backend::Desktop, Backend::Drm};

[[nodiscard]] constexpr std::string_view to_string(Backend b) noexcept {
  return b == Backend::Desktop ? "desktop" : "drm";
}

[[nodiscard]] std::optional<Backend> parse_backend(std::string_view s) noexcept;

enum class Source { Mock, Replay, Uart };
constexpr std::array<Source, 3> kAllSources = {Source::Mock, Source::Replay, Source::Uart};

[[nodiscard]] constexpr std::string_view to_string(Source s) noexcept {
  switch (s) {
    case Source::Mock:
      return "mock";
    case Source::Replay:
      return "replay";
    case Source::Uart:
      return "uart";
  }
  return "?";
}

[[nodiscard]] std::optional<Source> parse_source(std::string_view s) noexcept;

[[nodiscard]] std::optional<logging::Level> parse_log_level(std::string_view s) noexcept;

struct Config {
  // display
  Backend backend = Backend::Desktop;
  int width = 1600;  // the 800 × 1280 panel drawn at 2× (docs/c3-console-ui-design.md)
  int height = 2560;
  float dpi = 264.0F;  // of the target panel; sizes type and touch targets
  std::string drm_device = "/dev/dri/card0";
  int rotation = 0;  // panel mounting rotation, clockwise degrees: 0 | 90 | 180 | 270

  // telemetry
  Source source = Source::Mock;
  std::string uart_device = "/dev/ttyAMA0";
  int baud = 115200;
  int stall_timeout_ms = 1000;
  std::string mock_profile = "WarmupDrive";
  std::string replay_file;
  std::string record_file;  // empty = no recording

  // carplay
  std::uint16_t usb_vendor_id = 0x1314;
  std::vector<std::uint16_t> usb_product_ids = {0x1520, 0x1521};

  // audio
  std::string alsa_out = "default";
  std::string alsa_mic = "default";

  // ui defaults (settings sheet). `settings_file` on the data partition overrides them when
  // present and is rewritten on every change; empty = in memory only.
  render::Settings ui{};
  std::string settings_file;

  // platform
  std::string watchdog_device;  // e.g. /dev/watchdog on the target; empty = no watchdog

  // files
  std::string calibration_file = "config/calibration.json";
  std::string assets_dir = "assets";                // fonts; resolved like the config path
  logging::Level log_level = logging::Level::Info;  // parsed from `[logging] level` by load_toml()

  // run mode
  std::optional<std::uint32_t> headless_seconds;  // set by --headless; 0 = until interrupted
  std::optional<std::string> screenshot_path;     // set by --screenshot
  float screenshot_after_s = 0.5F;                // set by --screenshot-after
  std::optional<std::string> open_surface;        // set by --open; validated name

  // provenance
  std::string config_path;  // file actually loaded
};

/// Locates a project-relative path (config/console.toml, assets/): the working directory
/// first, then relative to the executable (build/ and build/*/ layouts). Returns `relative`
/// unchanged when nothing is found so the resulting open error names it.
[[nodiscard]] std::string find_project_path(std::string_view relative, std::string_view argv0);

/// Explicit --config wins; otherwise find_project_path("config/console.toml").
[[nodiscard]] std::string find_config_path(const std::optional<std::string>& explicit_path, std::string_view argv0);

/// Overlay values from a TOML file onto `cfg`. Enumerated values (backend, source, theme, units,
/// log level) are parsed here, so a Config never carries an unrecognised word.
/// Throws std::runtime_error (unreadable file, wrong-typed or unknown value) or toml::ParseError.
void load_toml(Config& cfg, const std::string& path);

/// Range and consistency checks, including, for the mock source, the profile name.
/// Returns the first problem in field order, or nullopt when the config is usable.
[[nodiscard]] std::optional<std::string> validate(const Config& cfg);

/// Human-readable dump, one `key = value` per line.
[[nodiscard]] std::string describe(const Config& cfg);

enum class ResolveStatus {
  Ok,
  UsageError,   // a flag value was malformed; show usage
  ConfigError,  // the file or the combined result is unusable
};

struct ConfigResolution {
  ResolveStatus status = ResolveStatus::Ok;
  Config config;
  std::string error;                  // set unless status == Ok
  std::vector<std::string> warnings;  // non-fatal notes for the log
};

/// defaults <- file <- flags, then validate. Never throws.
///
/// A file that cannot be read or parsed is a ConfigError: the console does not start on
/// defaults alone. A flag whose value does not parse is a UsageError; a combination validate()
/// rejects is a ConfigError. `--replay=FILE` selects the replay source unless `--source` says
/// otherwise, in which case validate() reports the conflict.
[[nodiscard]] ConfigResolution resolve_config(const Args& args, const std::string& config_path);

}  // namespace c3::app
