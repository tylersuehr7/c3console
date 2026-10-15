// config.cpp — defaults <- console.toml <- flags: loading, validation, resolution, description.
#include "app/args.hpp"
#include "app/config.hpp"
#include "app/toml.hpp"
#include "logging/log.hpp"
#include "render/settings.hpp"
#include "render/units.hpp"
#include "telemetry/freshness.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <unistd.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using c3::app::Args;
using c3::app::Backend;
using c3::app::Config;
using c3::app::ConfigResolution;
using c3::app::describe;
using c3::app::find_config_path;
using c3::app::find_project_path;
using c3::app::kAllBackends;
using c3::app::kAllSources;
using c3::app::load_toml;
using c3::app::parse_backend;
using c3::app::parse_log_level;
using c3::app::parse_source;
using c3::app::resolve_config;
using c3::app::ResolveStatus;
using c3::app::Source;
using c3::app::to_string;
using c3::app::validate;
using c3::logging::kAllLevels;
using c3::logging::Level;
using c3::render::PressureUnit;
using c3::render::TempUnit;
using c3::render::ThemePreference;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;
using Catch::Matchers::Message;
using Catch::Matchers::WithinAbs;

CATCH_REGISTER_ENUM(Backend, Backend::Desktop, Backend::Drm)
CATCH_REGISTER_ENUM(Source, Source::Mock, Source::Replay, Source::Uart)
CATCH_REGISTER_ENUM(ResolveStatus, ResolveStatus::Ok, ResolveStatus::UsageError, ResolveStatus::ConfigError)

namespace {

namespace fs = std::filesystem;

class ScratchDir {
public:
  ScratchDir() {
    static int counter = 0;
    path_ = fs::temp_directory_path() / ("c3_config_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
    fs::create_directories(path_);
  }
  ~ScratchDir() {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }
  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;
  ScratchDir(ScratchDir&&) = delete;
  ScratchDir& operator=(ScratchDir&&) = delete;

  [[nodiscard]] std::string file(std::string_view name, std::string_view content) const {
    const fs::path p = path_ / name;
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << content;
    return p.string();
  }
  [[nodiscard]] std::string path_of(std::string_view name) const { return (path_ / name).string(); }
  [[nodiscard]] const fs::path& path() const noexcept { return path_; }

private:
  fs::path path_;
};

constexpr std::string_view kFullToml = R"([display]
backend = "drm"
width = 800
height = 1280
dpi = 132.5
drm_device = "/dev/dri/card1"
rotation = 90
assets_dir = "/opt/c3/assets"

[telemetry]
source = "uart"
uart_device = "/dev/ttyUSB0"
baud = 230400
stall_timeout_ms = 500
mock_profile = "Overheat"
replay_file = ""
record_file = "/data/drive.csv"

[carplay]
usb_vendor_id = 0x1314
usb_product_ids = [0x1521]

[audio]
alsa_out = "hw:0"
alsa_mic = "hw:1"

[ui]
theme = "auto"
accent = 0x2DD4BF
temperature = "C"
pressure = "bar"
settings_file = "/data/settings.toml"

[platform]
watchdog_device = "/dev/watchdog"

[calibration]
file = "/data/calibration.json"

[logging]
level = "debug"
)";

/// Resolves `args` against a config written to a scratch file.
[[nodiscard]] ConfigResolution resolve(const ScratchDir& dir, std::string_view toml, const Args& args = {}) {
  return resolve_config(args, dir.file("console.toml", toml));
}

[[nodiscard]] Config load(const ScratchDir& dir, std::string_view toml) {
  Config cfg;
  load_toml(cfg, dir.file("console.toml", toml));
  return cfg;
}

// ---------------------------------------------------------------------------
// enums and defaults
// ---------------------------------------------------------------------------

TEST_CASE("config: backend and source names round-trip and reject anything else", "[config]") {
  STATIC_CHECK(to_string(Backend::Desktop) == "desktop");
  STATIC_CHECK(to_string(Backend::Drm) == "drm");
  STATIC_CHECK(to_string(Source::Mock) == "mock");
  STATIC_CHECK(to_string(Source::Replay) == "replay");
  STATIC_CHECK(to_string(Source::Uart) == "uart");
  for (const Backend b : kAllBackends) {
    CHECK(parse_backend(to_string(b)) == b);
  }
  for (const Source s : kAllSources) {
    CHECK(parse_source(to_string(s)) == s);
  }
  CHECK_FALSE(parse_backend("DRM").has_value());
  CHECK_FALSE(parse_backend("sdl").has_value());
  CHECK_FALSE(parse_source("Mock").has_value());
  CHECK_FALSE(parse_source("").has_value());
}

TEST_CASE("config: the defaults are the development desktop at the mockup geometry", "[config]") {
  const Config cfg;
  CHECK(cfg.backend == Backend::Desktop);
  CHECK(cfg.width == 1600);
  CHECK(cfg.height == 2560);
  CHECK(cfg.dpi == 264.0F);
  CHECK(cfg.rotation == 0);
  CHECK(cfg.source == Source::Mock);
  CHECK(cfg.mock_profile == "WarmupDrive");
  CHECK(cfg.baud == 115200);
  CHECK(cfg.stall_timeout_ms == 1000);
  CHECK(cfg.usb_vendor_id == 0x1314);
  CHECK(cfg.usb_product_ids == std::vector<std::uint16_t>{0x1520, 0x1521});  // both dongle ids, never one
  CHECK(cfg.ui == c3::render::Settings{});
  CHECK(cfg.settings_file.empty());
  CHECK(cfg.watchdog_device.empty());
  CHECK(cfg.calibration_file == "config/calibration.json");
  CHECK(cfg.log_level == Level::Info);
  CHECK_FALSE(cfg.headless_seconds.has_value());
  CHECK_FALSE(cfg.screenshot_path.has_value());
  CHECK(cfg.screenshot_after_s == 0.5F);
  CHECK_FALSE(cfg.open_surface.has_value());
  CHECK_FALSE(validate(cfg).has_value());
}

// ---------------------------------------------------------------------------
// validate
// ---------------------------------------------------------------------------

TEST_CASE("validate: display geometry must be positive and the rotation a quarter turn", "[config]") {
  CHECK(validate(Config{.width = 0}) == "display width/height must be positive");
  CHECK(validate(Config{.height = -1}) == "display width/height must be positive");
  CHECK(validate(Config{.dpi = 0.0F}) == "display dpi must be positive");
  CHECK(validate(Config{.dpi = std::numeric_limits<float>::quiet_NaN()}) == "display dpi must be positive");
  CHECK(validate(Config{.rotation = 45}) == "[display] rotation must be 0, 90, 180 or 270");
  CHECK(validate(Config{.rotation = -90}) == "[display] rotation must be 0, 90, 180 or 270");
  for (const int r : {0, 90, 180, 270}) {
    CHECK_FALSE(validate(Config{.rotation = r}).has_value());
  }
}

TEST_CASE("validate: the stall timeout is bounded by the freshness limit and baud must be positive", "[config]") {
  CHECK(validate(Config{.stall_timeout_ms = 0}) == "telemetry stall_timeout_ms must be 1..1000");
  CHECK(validate(Config{.stall_timeout_ms = 1001}) == "telemetry stall_timeout_ms must be 1..1000");
  CHECK_FALSE(validate(Config{.stall_timeout_ms = 1}).has_value());
  CHECK_FALSE(validate(Config{.stall_timeout_ms = static_cast<int>(c3::telemetry::kMaxStallTimeoutMs)}).has_value());
  CHECK(validate(Config{.baud = 0}) == "telemetry baud must be positive");
}

TEST_CASE("validate: replay needs a file and a file needs replay", "[config]") {
  CHECK(validate(Config{.source = Source::Replay}) ==
        "source=replay requires --replay=FILE or [telemetry] replay_file");
  CHECK_FALSE(validate(Config{.source = Source::Replay, .replay_file = "drive.csv"}).has_value());
  CHECK(validate(Config{.source = Source::Mock, .replay_file = "drive.csv"}) ==
        "--replay given but source is 'mock'; use --source=replay");
  CHECK(validate(Config{.source = Source::Uart, .replay_file = "drive.csv"}) ==
        "--replay given but source is 'uart'; use --source=replay");
}

TEST_CASE("validate: at least one dongle product id is required", "[config]") {
  CHECK(validate(Config{.usb_product_ids = {}}) == "[carplay] usb_product_ids must list at least one product id");
  CHECK_FALSE(validate(Config{.usb_product_ids = {0x1520}}).has_value());
}

TEST_CASE("validate: the mock profile is checked only for the mock source and the message lists the profiles",
          "[config]") {
  const auto problem = validate(Config{.source = Source::Mock, .mock_profile = "Bogus"});
  REQUIRE(problem.has_value());
  CHECK_THAT(*problem, ContainsSubstring("mock_profile 'Bogus' is not one of: "));
  CHECK_THAT(*problem, ContainsSubstring("ColdStart"));
  CHECK_THAT(*problem, ContainsSubstring("WarmupDrive"));
  CHECK_THAT(*problem, ContainsSubstring("Overheat"));
  CHECK_FALSE(validate(Config{.source = Source::Mock, .mock_profile = "LinkLoss"}).has_value());
  CHECK_FALSE(validate(Config{.source = Source::Uart, .mock_profile = "Bogus"}).has_value());
  CHECK(validate(Config{.source = Source::Mock, .mock_profile = "warmupdrive"}).has_value());  // case-sensitive
}

TEST_CASE("parse_log_level: maps the four config words onto logger levels, exactly", "[config]") {
  STATIC_CHECK(kAllLevels.size() == 4);
  CHECK(parse_log_level("debug") == Level::Debug);
  CHECK(parse_log_level("info") == Level::Info);
  CHECK(parse_log_level("warn") == Level::Warn);
  CHECK(parse_log_level("error") == Level::Error);
  for (const Level level : kAllLevels) {  // spelled by the logger, so the round trip is exact
    CHECK(parse_log_level(c3::logging::to_string(level)) == level);
  }
  CHECK_FALSE(parse_log_level("INFO").has_value());
  CHECK_FALSE(parse_log_level("warning").has_value());
  CHECK_FALSE(parse_log_level("").has_value());
}

TEST_CASE("validate: reports the first problem in field order", "[config]") {
  CHECK(validate(Config{.width = 0, .rotation = 7, .stall_timeout_ms = 0}) == "display width/height must be positive");
}

// ---------------------------------------------------------------------------
// load_toml
// ---------------------------------------------------------------------------

TEST_CASE("load_toml: every supported key overlays its field and the path is recorded", "[config]") {
  const ScratchDir dir;
  const Config cfg = load(dir, kFullToml);
  CHECK(cfg.config_path == dir.path_of("console.toml"));
  CHECK(cfg.backend == Backend::Drm);
  CHECK(cfg.width == 800);
  CHECK(cfg.height == 1280);
  CHECK_THAT(cfg.dpi, WithinAbs(132.5, 1e-5));
  CHECK(cfg.drm_device == "/dev/dri/card1");
  CHECK(cfg.rotation == 90);
  CHECK(cfg.assets_dir == "/opt/c3/assets");
  CHECK(cfg.source == Source::Uart);
  CHECK(cfg.uart_device == "/dev/ttyUSB0");
  CHECK(cfg.baud == 230400);
  CHECK(cfg.stall_timeout_ms == 500);
  CHECK(cfg.mock_profile == "Overheat");
  CHECK(cfg.replay_file.empty());
  CHECK(cfg.record_file == "/data/drive.csv");
  CHECK(cfg.usb_vendor_id == 0x1314);
  CHECK(cfg.usb_product_ids == std::vector<std::uint16_t>{0x1521});
  CHECK(cfg.alsa_out == "hw:0");
  CHECK(cfg.alsa_mic == "hw:1");
  CHECK(cfg.ui.theme == ThemePreference::Auto);
  CHECK(cfg.ui.accent == 0x2DD4BF);
  CHECK(cfg.ui.units.temp == TempUnit::Celsius);
  CHECK(cfg.ui.units.pressure == PressureUnit::Bar);
  CHECK(cfg.settings_file == "/data/settings.toml");
  CHECK(cfg.watchdog_device == "/dev/watchdog");
  CHECK(cfg.calibration_file == "/data/calibration.json");
  CHECK(cfg.log_level == Level::Debug);
  CHECK_FALSE(validate(cfg).has_value());
}

TEST_CASE("load_toml: an empty file leaves every default in place", "[config]") {
  const ScratchDir dir;
  const Config loaded = load(dir, "");
  Config expected;
  expected.config_path = loaded.config_path;
  CHECK(loaded.backend == expected.backend);
  CHECK(loaded.width == expected.width);
  CHECK(loaded.usb_product_ids == expected.usb_product_ids);
  CHECK(loaded.ui == expected.ui);
  CHECK(loaded.log_level == expected.log_level);
  CHECK(loaded.mock_profile == expected.mock_profile);
  CHECK(describe(loaded) == describe(expected));
}

TEST_CASE("load_toml: the log level is read as a logger level, not kept as text", "[config]") {
  const ScratchDir dir;
  for (const Level level : kAllLevels) {
    const std::string toml = "[logging]\nlevel = \"" + std::string(c3::logging::to_string(level)) + "\"\n";
    CHECK(load(dir, toml).log_level == level);
  }
}

TEST_CASE("load_toml: a partial file overlays only what it names", "[config]") {
  const ScratchDir dir;
  const Config cfg = load(dir, "[telemetry]\nsource = \"replay\"\nreplay_file = \"drive.csv\"\n");
  CHECK(cfg.source == Source::Replay);
  CHECK(cfg.replay_file == "drive.csv");
  CHECK(cfg.backend == Backend::Desktop);
  CHECK(cfg.width == 1600);
  CHECK(cfg.baud == 115200);
}

TEST_CASE("load_toml: an integer dpi is accepted where a float is expected", "[config]") {
  const ScratchDir dir;
  CHECK(load(dir, "[display]\ndpi = 300\n").dpi == 300.0F);
}

TEST_CASE("load_toml: unknown enumeration values name the key and the choices", "[config]") {
  const ScratchDir dir;
  Config cfg;
  CHECK_THROWS_MATCHES(load_toml(cfg, dir.file("a.toml", "[display]\nbackend = \"sdl\"\n")), std::runtime_error,
                       Message("[display] backend: expected desktop|drm, got 'sdl'"));
  CHECK_THROWS_MATCHES(load_toml(cfg, dir.file("b.toml", "[telemetry]\nsource = \"serial\"\n")), std::runtime_error,
                       Message("[telemetry] source: expected mock|replay|uart, got 'serial'"));
  CHECK_THROWS_MATCHES(load_toml(cfg, dir.file("c.toml", "[ui]\ntheme = \"sepia\"\n")), std::runtime_error,
                       Message("[ui] theme: expected dark|light|auto, got 'sepia'"));
  CHECK_THROWS_MATCHES(load_toml(cfg, dir.file("d.toml", "[logging]\nlevel = \"INFO\"\n")), std::runtime_error,
                       Message("[logging] level: expected debug|info|warn|error, got 'INFO'"));
  CHECK_THROWS_MATCHES(load_toml(cfg, dir.file("e.toml", "[logging]\nlevel = \"trace\"\n")), std::runtime_error,
                       Message("[logging] level: expected debug|info|warn|error, got 'trace'"));
}

TEST_CASE("load_toml: wrongly typed values are reported with section, key and expected type", "[config]") {
  const ScratchDir dir;
  Config cfg;
  CHECK_THROWS_MATCHES(load_toml(cfg, dir.file("a.toml", "[display]\nwidth = \"wide\"\n")), std::runtime_error,
                       Message("[display] width: expected integer"));
  CHECK_THROWS_MATCHES(load_toml(cfg, dir.file("b.toml", "[display]\nbackend = 1\n")), std::runtime_error,
                       Message("[display] backend: expected string"));
  CHECK_THROWS_MATCHES(load_toml(cfg, dir.file("c.toml", "[carplay]\nusb_product_ids = 0x1520\n")), std::runtime_error,
                       Message("[carplay] usb_product_ids: expected array of integers"));
  CHECK_THROWS_MATCHES(load_toml(cfg, dir.file("d.toml", "[display]\ndpi = \"high\"\n")), std::runtime_error,
                       Message("[display] dpi: expected number"));
}

TEST_CASE("load_toml: integers that do not fit their field are rejected", "[config]") {
  const ScratchDir dir;
  Config cfg;
  CHECK_THROWS_MATCHES(load_toml(cfg, dir.file("a.toml", "[display]\nwidth = 4294967296\n")), std::runtime_error,
                       Message("[display] width: out of range"));
  CHECK_THROWS_MATCHES(load_toml(cfg, dir.file("b.toml", "[carplay]\nusb_vendor_id = 0x10000\n")), std::runtime_error,
                       Message("[carplay] usb_vendor_id: not a 16-bit value"));
  CHECK_THROWS_MATCHES(load_toml(cfg, dir.file("c.toml", "[carplay]\nusb_product_ids = [0x1520, -1]\n")),
                       std::runtime_error, Message("[carplay] usb_product_ids: not a 16-bit value"));
}

TEST_CASE("load_toml: an unreadable or malformed file throws with the path", "[config]") {
  const ScratchDir dir;
  Config cfg;
  const std::string missing = dir.path_of("absent.toml");
  CHECK_THROWS_MATCHES(load_toml(cfg, missing), std::runtime_error, Message("cannot open " + missing));
  const std::string bad = dir.file("bad.toml", "[display]\nwidth =\n");
  CHECK_THROWS_MATCHES(load_toml(cfg, bad), c3::toml::ParseError, Message(bad + ":line 2: missing value"));
}

TEST_CASE("load_toml: keys in the wrong section are ignored, so the file's layout matters", "[config]") {
  const ScratchDir dir;
  const Config cfg = load(dir, "width = 10\n[telemetry]\nbackend = \"drm\"\n");
  CHECK(cfg.width == 1600);
  CHECK(cfg.backend == Backend::Desktop);
}

// ---------------------------------------------------------------------------
// paths
// ---------------------------------------------------------------------------

TEST_CASE("find_project_path: prefers a path that exists relative to the working directory", "[config]") {
  // config/calibration.json ships with the repository, but the test may not run from its root,
  // so this case only asserts the unchanged return when the relative path exists.
  if (!fs::exists("config/calibration.json")) {
    SKIP("not running from the repository root");
  }
  CHECK(find_project_path("config/calibration.json", "/nowhere/c3console") == "config/calibration.json");
}

TEST_CASE("find_project_path: falls back to one and two directories above the executable", "[config]") {
  const ScratchDir dir;
  const std::string target = dir.file("config/console_test.toml", "");
  // The executable's directory must exist for ".." to resolve, as it always does for a real binary.
  fs::create_directories(dir.path() / "build" / "tests");
  constexpr std::string_view kRelative = "config/console_test.toml";
  REQUIRE_FALSE(fs::exists(kRelative));  // otherwise the working-directory branch would win

  const std::string from_build = find_project_path(kRelative, dir.path_of("build/c3console"));
  CHECK(fs::equivalent(from_build, target));
  const std::string from_tests = find_project_path(kRelative, dir.path_of("build/tests/c3console_tests"));
  CHECK(fs::equivalent(from_tests, target));
  CHECK(fs::path(from_build).is_absolute());
}

TEST_CASE("find_project_path: returns the relative path unchanged when nothing is found", "[config]") {
  const ScratchDir dir;
  fs::create_directories(dir.path() / "build");
  CHECK(find_project_path("config/zz_absent.toml", dir.path_of("build/c3console")) == "config/zz_absent.toml");
  CHECK(find_project_path("config/zz_absent.toml", "") == "config/zz_absent.toml");
}

TEST_CASE("find_config_path: an explicit --config wins even if it does not exist", "[config]") {
  CHECK(find_config_path(std::string("/etc/c3/custom.toml"), "/nowhere/c3console") == "/etc/c3/custom.toml");
}

TEST_CASE("find_config_path: without --config the shipped file is located like any project path", "[config]") {
  const ScratchDir dir;
  const std::string shipped = dir.file("config/console.toml", "");
  fs::create_directories(dir.path() / "build");
  const std::string found = find_config_path(std::nullopt, dir.path_of("build/c3console"));
  if (fs::exists("config/console.toml")) {
    // Run from the repository root, the working-directory copy wins, as it does for the real binary.
    CHECK(found == "config/console.toml");
  } else {
    CHECK(fs::equivalent(found, shipped));
  }
}

// ---------------------------------------------------------------------------
// describe
// ---------------------------------------------------------------------------

TEST_CASE("describe: lists one key per line with hex ids and the source-specific detail", "[config]") {
  Config cfg;
  cfg.config_path = "config/console.toml";
  const std::string text = describe(cfg);
  CHECK_THAT(text, ContainsSubstring("config_path      = config/console.toml\n"));
  CHECK_THAT(text, ContainsSubstring("backend          = desktop\n"));
  CHECK_THAT(text, ContainsSubstring("display          = 1600 x 2560\n"));
  CHECK_THAT(text, ContainsSubstring("source           = mock\n"));
  CHECK_THAT(text, ContainsSubstring("mock_profile     = WarmupDrive\n"));
  CHECK_THAT(text, ContainsSubstring("usb_vendor_id    = 0x1314\n"));
  CHECK_THAT(text, ContainsSubstring("usb_product_ids  = 0x1520 0x1521\n"));
  CHECK_THAT(text, ContainsSubstring("ui               = dark, accent #FF6A2B, F, psi\n"));
  CHECK_THAT(text, ContainsSubstring("settings_file    = (in memory)\n"));
  CHECK_THAT(text, ContainsSubstring("watchdog_device  = (none)\n"));
  CHECK_THAT(text, ContainsSubstring("log_level        = info\n"));
  CHECK_THAT(text, !ContainsSubstring("replay_file"));
  CHECK_THAT(text, !ContainsSubstring("uart_device"));
  CHECK_THAT(text, !ContainsSubstring("record_file"));
  CHECK_THAT(text, !ContainsSubstring("headless"));
  CHECK_THAT(text, !ContainsSubstring("screenshot"));
  CHECK_THAT(text, EndsWith("\n"));
}

TEST_CASE("describe: shows replay, uart, recording and run-mode details when they apply", "[config]") {
  Config cfg{.source = Source::Replay, .replay_file = "drive.csv", .record_file = "out.csv"};
  cfg.headless_seconds = 30;
  cfg.screenshot_path = "panel.ppm";
  cfg.screenshot_after_s = 1.5F;
  cfg.settings_file = "/data/s.toml";
  cfg.watchdog_device = "/dev/watchdog";
  const std::string text = describe(cfg);
  CHECK_THAT(text, ContainsSubstring("replay_file      = drive.csv\n"));
  CHECK_THAT(text, ContainsSubstring("record_file      = out.csv\n"));
  CHECK_THAT(text, ContainsSubstring("headless         = 30 s\n"));
  CHECK_THAT(text, ContainsSubstring("screenshot       = panel.ppm after 1.5 s\n"));
  CHECK_THAT(text, ContainsSubstring("settings_file    = /data/s.toml\n"));
  CHECK_THAT(text, ContainsSubstring("watchdog_device  = /dev/watchdog\n"));
  CHECK_THAT(text, !ContainsSubstring("mock_profile"));

  const std::string uart = describe(Config{.source = Source::Uart, .uart_device = "/dev/ttyAMA0", .baud = 115200});
  CHECK_THAT(uart, ContainsSubstring("uart_device      = /dev/ttyAMA0 @ 115200 8N1\n"));
}

// ---------------------------------------------------------------------------
// resolve_config
// ---------------------------------------------------------------------------

TEST_CASE("resolve_config: a readable file with no flags resolves to its content", "[config]") {
  const ScratchDir dir;
  const ConfigResolution r = resolve(dir, kFullToml);
  CHECK(r.status == ResolveStatus::Ok);
  CHECK(r.error.empty());
  CHECK(r.warnings.empty());
  CHECK(r.config.backend == Backend::Drm);
  CHECK(r.config.source == Source::Uart);
  CHECK(r.config.config_path == dir.path_of("console.toml"));
}

TEST_CASE("resolve_config: an empty file resolves to the defaults", "[config]") {
  const ScratchDir dir;
  const ConfigResolution r = resolve(dir, "");
  CHECK(r.status == ResolveStatus::Ok);
  CHECK(r.config.backend == Backend::Desktop);
  CHECK(r.config.source == Source::Mock);
  CHECK(r.config.mock_profile == "WarmupDrive");
}

TEST_CASE("resolve_config: a missing or malformed file is a ConfigError; the console does not start on defaults",
          "[config]") {
  const ScratchDir dir;
  const std::string missing = dir.path_of("absent.toml");
  const ConfigResolution gone = resolve_config(Args{}, missing);
  CHECK(gone.status == ResolveStatus::ConfigError);
  CHECK(gone.error == "cannot open " + missing);

  const ConfigResolution bad = resolve(dir, "[display]\nwidth = \"x\"\n");
  CHECK(bad.status == ResolveStatus::ConfigError);
  CHECK(bad.error == "[display] width: expected integer");

  const ConfigResolution syntax = resolve(dir, "[display\n");
  CHECK(syntax.status == ResolveStatus::ConfigError);
  CHECK_THAT(syntax.error, EndsWith(":line 1: malformed section header"));
}

TEST_CASE("resolve_config: --desktop overrides the file's backend", "[config]") {
  const ScratchDir dir;
  const ConfigResolution r = resolve(dir, "[display]\nbackend = \"drm\"\n", Args{.desktop = true});
  CHECK(r.status == ResolveStatus::Ok);
  CHECK(r.config.backend == Backend::Desktop);
}

TEST_CASE("resolve_config: --source overrides the file and must be a known source", "[config]") {
  const ScratchDir dir;
  const ConfigResolution ok = resolve(dir, "[telemetry]\nsource = \"mock\"\n", Args{.source = "uart"});
  CHECK(ok.status == ResolveStatus::Ok);
  CHECK(ok.config.source == Source::Uart);

  const ConfigResolution bad = resolve(dir, "", Args{.source = "serial"});
  CHECK(bad.status == ResolveStatus::UsageError);
  CHECK(bad.error == "--source must be one of mock|replay|uart, got 'serial'");
}

TEST_CASE("resolve_config: --replay implies the replay source unless --source says otherwise", "[config]") {
  const ScratchDir dir;
  const ConfigResolution implied = resolve(dir, "", Args{.replay = "drive.csv"});
  CHECK(implied.status == ResolveStatus::Ok);
  CHECK(implied.config.source == Source::Replay);
  CHECK(implied.config.replay_file == "drive.csv");

  const ConfigResolution explicit_replay = resolve(dir, "", Args{.source = "replay", .replay = "drive.csv"});
  CHECK(explicit_replay.status == ResolveStatus::Ok);

  const ConfigResolution conflict = resolve(dir, "", Args{.source = "mock", .replay = "drive.csv"});
  CHECK(conflict.status == ResolveStatus::ConfigError);
  CHECK(conflict.error == "--replay given but source is 'mock'; use --source=replay");

  const ConfigResolution no_file = resolve(dir, "", Args{.source = "replay"});
  CHECK(no_file.status == ResolveStatus::ConfigError);
  CHECK(no_file.error == "source=replay requires --replay=FILE or [telemetry] replay_file");

  // the file may supply the replay path on its own
  const ConfigResolution from_file = resolve(dir, "[telemetry]\nsource = \"replay\"\nreplay_file = \"logged.csv\"\n");
  CHECK(from_file.status == ResolveStatus::Ok);
  CHECK(from_file.config.replay_file == "logged.csv");
}

TEST_CASE("resolve_config: --headless takes whole non-negative seconds", "[config]") {
  const ScratchDir dir;
  CHECK(resolve(dir, "", Args{.headless = "30"}).config.headless_seconds == 30U);
  CHECK(resolve(dir, "", Args{.headless = "0"}).config.headless_seconds == 0U);
  for (const char* bad : {"x", "-1", "1.5", "", " 5", "5s"}) {
    INFO(bad);
    const ConfigResolution r = resolve(dir, "", Args{.headless = bad});
    CHECK(r.status == ResolveStatus::UsageError);
    CHECK(r.error == std::string("--headless expects whole seconds, got '") + bad + "'");
  }
  CHECK_FALSE(resolve(dir, "").config.headless_seconds.has_value());
}

TEST_CASE("resolve_config: --screenshot and --screenshot-after", "[config]") {
  const ScratchDir dir;
  const ConfigResolution r = resolve(dir, "", Args{.screenshot = "panel.ppm", .screenshot_after = "1.25"});
  CHECK(r.status == ResolveStatus::Ok);
  CHECK(r.config.screenshot_path == "panel.ppm");
  CHECK_THAT(r.config.screenshot_after_s, WithinAbs(1.25, 1e-6));
  CHECK(resolve(dir, "", Args{.screenshot_after = "0"}).config.screenshot_after_s == 0.0F);
  CHECK(resolve(dir, "", Args{.screenshot = "p.ppm"}).config.screenshot_after_s == 0.5F);  // the default delay

  for (const char* bad : {"-1", "abc", "1.5s", "", "inf", "nan"}) {
    INFO(bad);
    const ConfigResolution e = resolve(dir, "", Args{.screenshot_after = bad});
    CHECK(e.status == ResolveStatus::UsageError);
    CHECK(e.error == std::string("--screenshot-after expects seconds >= 0, got '") + bad + "'");
  }
}

TEST_CASE("resolve_config: --record overrides the file's record_file", "[config]") {
  const ScratchDir dir;
  const ConfigResolution r = resolve(dir, "[telemetry]\nrecord_file = \"a.csv\"\n", Args{.record = "b.csv"});
  CHECK(r.config.record_file == "b.csv");
  CHECK(resolve(dir, "[telemetry]\nrecord_file = \"a.csv\"\n").config.record_file == "a.csv");
}

TEST_CASE("resolve_config: --open accepts the seven surface names and nothing else", "[config]") {
  const ScratchDir dir;
  for (const char* name : {"camera", "settings", "audio", "theme", "units", "calibration", "system"}) {
    INFO(name);
    const ConfigResolution r = resolve(dir, "", Args{.open = name});
    CHECK(r.status == ResolveStatus::Ok);
    CHECK(r.config.open_surface == name);
  }
  for (const char* bad : {"Camera", "carplay", "settings ", ""}) {
    INFO(bad);
    const ConfigResolution r = resolve(dir, "", Args{.open = bad});
    CHECK(r.status == ResolveStatus::UsageError);
    CHECK(r.error ==
          std::string("--open expects camera|settings|audio|theme|units|calibration|system, got '") + bad + "'");
  }
}

TEST_CASE("resolve_config: --profile overrides the mock profile and warns when the source is not mock", "[config]") {
  const ScratchDir dir;
  const ConfigResolution mock = resolve(dir, "", Args{.profile = "Overheat"});
  CHECK(mock.status == ResolveStatus::Ok);
  CHECK(mock.config.mock_profile == "Overheat");
  CHECK(mock.warnings.empty());

  const ConfigResolution uart = resolve(dir, "", Args{.source = "uart", .profile = "Overheat"});
  CHECK(uart.status == ResolveStatus::Ok);
  REQUIRE(uart.warnings.size() == 1);
  CHECK(uart.warnings[0] == "--profile is only used with --source=mock; ignoring");

  const ConfigResolution bad = resolve(dir, "", Args{.profile = "Bogus"});
  CHECK(bad.status == ResolveStatus::ConfigError);
  CHECK_THAT(bad.error, ContainsSubstring("mock_profile 'Bogus' is not one of: "));
}

TEST_CASE("resolve_config: flags are checked before the combined result is validated", "[config]") {
  const ScratchDir dir;
  // a bad flag value is a UsageError even when the file is also unusable
  const ConfigResolution r = resolve(dir, "[display]\nrotation = 45\n", Args{.headless = "x"});
  CHECK(r.status == ResolveStatus::UsageError);
  // and the file's problem surfaces once the flags are fine
  const ConfigResolution v = resolve(dir, "[display]\nrotation = 45\n", Args{.headless = "1"});
  CHECK(v.status == ResolveStatus::ConfigError);
  CHECK(v.error == "[display] rotation must be 0, 90, 180 or 270");
}

TEST_CASE("resolve_config: a full development command line against a full file", "[config]") {
  const ScratchDir dir;
  const Args args{.desktop = true,
                  .source = "mock",
                  .profile = "HighwayCruise",
                  .headless = "10",
                  .screenshot = "p.ppm",
                  .screenshot_after = "2",
                  .open = "audio",
                  .record = "rec.csv"};
  const ConfigResolution r = resolve(dir, kFullToml, args);
  REQUIRE(r.status == ResolveStatus::Ok);
  CHECK(r.config.backend == Backend::Desktop);  // flag
  CHECK(r.config.width == 800);                 // file
  CHECK(r.config.source == Source::Mock);
  CHECK(r.config.mock_profile == "HighwayCruise");
  CHECK(r.config.headless_seconds == 10U);
  CHECK(r.config.screenshot_path == "p.ppm");
  CHECK(r.config.screenshot_after_s == 2.0F);
  CHECK(r.config.open_surface == "audio");
  CHECK(r.config.record_file == "rec.csv");
  CHECK(r.config.ui.accent == 0x2DD4BF);  // file
  CHECK(r.warnings.empty());
}

}  // namespace
