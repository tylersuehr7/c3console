#include "config.hpp"

#include "logging/log.hpp"
#include "settings_store.hpp"
#include "telemetry/freshness.hpp"
#include "telemetry/mock_source.hpp"
#include "toml.hpp"
#include "utils/join_enum_names.hpp"
#include "utils/parse_enum_name.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace c3::app {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kDefaultConfig = "config/console.toml";

[[nodiscard]] int checked_int(std::int64_t v, std::string_view what) {
  if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max()) {
    throw std::runtime_error(std::string(what) + ": out of range");
  }
  return static_cast<int>(v);
}

[[nodiscard]] std::uint16_t checked_u16(std::int64_t v, std::string_view what) {
  if (v < 0 || v > std::numeric_limits<std::uint16_t>::max()) {
    throw std::runtime_error(std::string(what) + ": not a 16-bit value");
  }
  return static_cast<std::uint16_t>(v);
}

[[nodiscard]] std::string hex4(std::uint16_t v) {
  constexpr std::string_view kDigits = "0123456789abcdef";
  std::string s = "0x";
  for (int shift = 12; shift >= 0; shift -= 4) {
    s.push_back(kDigits[(v >> shift) & 0xFU]);
  }
  return s;
}

[[nodiscard]] std::string hex6(std::uint32_t v) {
  constexpr std::string_view kDigits = "0123456789ABCDEF";
  std::string s = "#";
  for (int shift = 20; shift >= 0; shift -= 4) {
    s.push_back(kDigits[(v >> shift) & 0xFU]);
  }
  return s;
}

[[nodiscard]] ConfigResolution fail(ResolveStatus status, std::string error) {
  ConfigResolution r;
  r.status = status;
  r.error = std::move(error);
  return r;
}

// Whole non-negative seconds, nothing else.
[[nodiscard]] std::optional<std::uint32_t> parse_seconds(std::string_view text) noexcept {
  std::uint32_t seconds = 0;
  const char* const last = std::next(text.data(), static_cast<std::ptrdiff_t>(text.size()));
  const auto res = std::from_chars(text.data(), last, seconds);
  if (res.ec != std::errc{} || res.ptr != last) {
    return std::nullopt;
  }
  return seconds;
}

// The profile names, for the error message. MockProfile has no kAll array; walk the enum.
[[nodiscard]] std::string mock_profile_names() {
  std::string out;
  for (std::size_t i = 0; i < static_cast<std::size_t>(telemetry::MockProfile::Count); ++i) {
    if (!out.empty()) {
      out += ", ";
    }
    out += telemetry::to_string(static_cast<telemetry::MockProfile>(i));
  }
  return out;
}

}  // namespace

std::optional<Backend> parse_backend(std::string_view s) noexcept {
  return utils::parse_enum(s, kAllBackends);
}

std::optional<Source> parse_source(std::string_view s) noexcept {
  return utils::parse_enum(s, kAllSources);
}

std::optional<logging::Level> parse_log_level(std::string_view s) noexcept {
  return utils::parse_enum(s, logging::kAllLevels);
}

std::string find_project_path(std::string_view relative, std::string_view argv0) {
  std::error_code ec;
  if (fs::exists(relative, ec)) {
    return std::string(relative);
  }

  const fs::path exe = fs::weakly_canonical(fs::absolute(fs::path(argv0), ec), ec);
  if (ec) {
    return std::string(relative);
  }

  for (const char* up : {"..", "../.."}) {
    const fs::path candidate = exe.parent_path() / up / relative;
    if (fs::exists(candidate, ec)) {
      return fs::weakly_canonical(candidate, ec).string();
    }
  }

  return std::string(relative);
}

std::string find_config_path(const std::optional<std::string>& explicit_path, std::string_view argv0) {
  return explicit_path ? *explicit_path : find_project_path(kDefaultConfig, argv0);
}

void load_toml(Config& cfg, const std::string& path) {
  const toml::Document doc = toml::parse_file(path);
  cfg.config_path = path;

  const std::string backend = toml::get_string(doc, "display", "backend", std::string(to_string(cfg.backend)));
  const auto parsed_backend = parse_backend(backend);
  if (!parsed_backend) {
    throw std::runtime_error("[display] backend: expected desktop|drm, got '" + backend + "'");
  }
  cfg.backend = *parsed_backend;
  cfg.width = checked_int(toml::get_int(doc, "display", "width", cfg.width), "[display] width");
  cfg.height = checked_int(toml::get_int(doc, "display", "height", cfg.height), "[display] height");
  cfg.drm_device = toml::get_string(doc, "display", "drm_device", cfg.drm_device);
  cfg.rotation = checked_int(toml::get_int(doc, "display", "rotation", cfg.rotation), "[display] rotation");
  cfg.dpi = static_cast<float>(toml::get_float(doc, "display", "dpi", cfg.dpi));

  const std::string source = toml::get_string(doc, "telemetry", "source", std::string(to_string(cfg.source)));
  const auto parsed_source = parse_source(source);
  if (!parsed_source) {
    throw std::runtime_error("[telemetry] source: expected mock|replay|uart, got '" + source + "'");
  }
  cfg.source = *parsed_source;
  cfg.uart_device = toml::get_string(doc, "telemetry", "uart_device", cfg.uart_device);
  cfg.baud = checked_int(toml::get_int(doc, "telemetry", "baud", cfg.baud), "[telemetry] baud");
  cfg.stall_timeout_ms = checked_int(toml::get_int(doc, "telemetry", "stall_timeout_ms", cfg.stall_timeout_ms),
                                     "[telemetry] stall_timeout_ms");
  cfg.mock_profile = toml::get_string(doc, "telemetry", "mock_profile", cfg.mock_profile);
  cfg.replay_file = toml::get_string(doc, "telemetry", "replay_file", cfg.replay_file);
  cfg.record_file = toml::get_string(doc, "telemetry", "record_file", cfg.record_file);

  cfg.usb_vendor_id =
      checked_u16(toml::get_int(doc, "carplay", "usb_vendor_id", cfg.usb_vendor_id), "[carplay] usb_vendor_id");
  const std::vector<std::int64_t> default_pids(cfg.usb_product_ids.begin(), cfg.usb_product_ids.end());
  const auto pids = toml::get_int_array(doc, "carplay", "usb_product_ids", default_pids);
  cfg.usb_product_ids.clear();
  cfg.usb_product_ids.reserve(pids.size());
  for (const std::int64_t pid : pids) {
    cfg.usb_product_ids.push_back(checked_u16(pid, "[carplay] usb_product_ids"));
  }

  cfg.ui = parse_ui_settings(doc, cfg.ui);
  cfg.settings_file = toml::get_string(doc, "ui", "settings_file", cfg.settings_file);
  cfg.watchdog_device = toml::get_string(doc, "platform", "watchdog_device", cfg.watchdog_device);

  cfg.alsa_out = toml::get_string(doc, "audio", "alsa_out", cfg.alsa_out);
  cfg.alsa_mic = toml::get_string(doc, "audio", "alsa_mic", cfg.alsa_mic);

  cfg.calibration_file = toml::get_string(doc, "calibration", "file", cfg.calibration_file);
  cfg.assets_dir = toml::get_string(doc, "display", "assets_dir", cfg.assets_dir);

  const std::string level = toml::get_string(doc, "logging", "level", std::string(logging::to_string(cfg.log_level)));
  const auto parsed_level = parse_log_level(level);
  if (!parsed_level) {
    throw std::runtime_error("[logging] level: expected " + utils::join_names(logging::kAllLevels, "|") + ", got '" +
                             level + "'");
  }
  cfg.log_level = *parsed_level;
}

// Every rule names the field the way the file spells it, so the message is also the fix.
std::optional<std::string> validate(const Config& cfg) {
  if (cfg.width <= 0 || cfg.height <= 0) {
    return "display width/height must be positive";
  }
  if (!(cfg.dpi > 0.0F)) {
    return "display dpi must be positive";
  }
  if (cfg.rotation != 0 && cfg.rotation != 90 && cfg.rotation != 180 && cfg.rotation != 270) {
    return "[display] rotation must be 0, 90, 180 or 270";
  }
  if (cfg.stall_timeout_ms <= 0 || cfg.stall_timeout_ms > static_cast<int>(telemetry::kMaxStallTimeoutMs)) {
    return "telemetry stall_timeout_ms must be 1..1000";
  }
  if (cfg.baud <= 0) {
    return "telemetry baud must be positive";
  }
  if (cfg.source == Source::Replay && cfg.replay_file.empty()) {
    return "source=replay requires --replay=FILE or [telemetry] replay_file";
  }
  if (cfg.source != Source::Replay && !cfg.replay_file.empty()) {
    return "--replay given but source is '" + std::string(to_string(cfg.source)) + "'; use --source=replay";
  }
  if (cfg.usb_product_ids.empty()) {
    return "[carplay] usb_product_ids must list at least one product id";
  }
  if (cfg.source == Source::Mock && !telemetry::parse_mock_profile(cfg.mock_profile)) {
    return "mock_profile '" + cfg.mock_profile + "' is not one of: " + mock_profile_names();
  }
  return std::nullopt;
}

std::string describe(const Config& cfg) {
  std::ostringstream os;
  os << "config_path      = " << cfg.config_path << '\n'
     << "backend          = " << to_string(cfg.backend) << '\n'
     << "display          = " << cfg.width << " x " << cfg.height << '\n'
     << "dpi              = " << cfg.dpi << '\n'
     << "drm_device       = " << cfg.drm_device << " rotation " << cfg.rotation << '\n'
     << "source           = " << to_string(cfg.source) << '\n';

  switch (cfg.source) {
    case Source::Mock:
      os << "mock_profile     = " << cfg.mock_profile << '\n';
      break;
    case Source::Replay:
      os << "replay_file      = " << cfg.replay_file << '\n';
      break;
    case Source::Uart:
      os << "uart_device      = " << cfg.uart_device << " @ " << cfg.baud << " 8N1\n";
      break;
  }

  if (!cfg.record_file.empty()) {
    os << "record_file      = " << cfg.record_file << '\n';
  }

  os << "stall_timeout_ms = " << cfg.stall_timeout_ms << '\n'
     << "usb_vendor_id    = " << hex4(cfg.usb_vendor_id) << '\n'
     << "usb_product_ids  =";

  for (const std::uint16_t pid : cfg.usb_product_ids) {
    os << ' ' << hex4(pid);
  }

  os << '\n';
  if (cfg.headless_seconds) {
    os << "headless         = " << *cfg.headless_seconds << " s\n";
  }

  if (cfg.screenshot_path) {
    os << "screenshot       = " << *cfg.screenshot_path << " after " << cfg.screenshot_after_s << " s\n";
  }

  os << "ui               = " << to_string(cfg.ui.theme) << ", accent " << hex6(cfg.ui.accent) << ", "
     << to_string(cfg.ui.units.temp) << ", " << to_string(cfg.ui.units.pressure) << '\n'
     << "settings_file    = " << (cfg.settings_file.empty() ? "(in memory)" : cfg.settings_file) << '\n'
     << "watchdog_device  = " << (cfg.watchdog_device.empty() ? "(none)" : cfg.watchdog_device) << '\n'
     << "alsa_out         = " << cfg.alsa_out << '\n'
     << "alsa_mic         = " << cfg.alsa_mic << '\n'
     << "assets_dir       = " << cfg.assets_dir << '\n'
     << "calibration_file = " << cfg.calibration_file << '\n'
     << "log_level        = " << logging::to_string(cfg.log_level) << '\n';

  return os.str();
}

ConfigResolution resolve_config(const Args& args, const std::string& config_path) {
  ConfigResolution r;
  Config& cfg = r.config;
  try {
    load_toml(cfg, config_path);
  } catch (const std::exception& e) {
    return fail(ResolveStatus::ConfigError, e.what());
  }

  // Flags overlay the file.
  if (args.desktop) {
    cfg.backend = Backend::Desktop;
  }

  if (args.replay) {
    cfg.replay_file = *args.replay;
    if (!args.source) {
      cfg.source = Source::Replay;
    }
  }

  if (args.source) {
    const auto s = parse_source(*args.source);

    if (!s) {
      return fail(ResolveStatus::UsageError,
                  "--source must be one of " + utils::join_names(kAllSources, "|") + ", got '" + *args.source + "'");
    }

    cfg.source = *s;
  }

  if (args.headless) {
    cfg.headless_seconds = parse_seconds(*args.headless);
    if (!cfg.headless_seconds) {
      return fail(ResolveStatus::UsageError, "--headless expects whole seconds, got '" + *args.headless + "'");
    }
  }

  if (args.screenshot) {
    cfg.screenshot_path = *args.screenshot;
  }

  if (args.screenshot_after) {
    const char* const text = args.screenshot_after->c_str();
    char* end = nullptr;
    const float seconds = std::strtof(text, &end);

    // strtof reports "nothing parsed" by leaving `end` at the start, so an empty value is caught.
    if (end == text || *end != '\0' || !std::isfinite(seconds) || seconds < 0.0F) {
      return fail(ResolveStatus::UsageError,
                  "--screenshot-after expects seconds >= 0, got '" + *args.screenshot_after + "'");
    }

    cfg.screenshot_after_s = seconds;
  }

  if (args.record) {
    cfg.record_file = *args.record;
  }

  if (args.open) {
    constexpr std::array<std::string_view, 7> kSurfaces = {"camera", "settings",    "audio", "theme",
                                                           "units",  "calibration", "system"};

    if (std::find(kSurfaces.begin(), kSurfaces.end(), *args.open) == kSurfaces.end()) {
      return fail(ResolveStatus::UsageError,
                  "--open expects camera|settings|audio|theme|units|calibration|system, got '" + *args.open + "'");
    }

    cfg.open_surface = *args.open;
  }

  if (args.profile) {
    cfg.mock_profile = *args.profile;
    if (cfg.source != Source::Mock) {
      r.warnings.emplace_back("--profile is only used with --source=mock; ignoring");
    }
  }

  if (const auto problem = validate(cfg)) {
    return fail(ResolveStatus::ConfigError, *problem);
  }

  return r;
}

}  // namespace c3::app
