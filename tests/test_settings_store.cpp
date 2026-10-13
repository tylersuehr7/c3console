// settings_store.cpp — the [ui] table on the data partition: parse, format, load, atomic save.
#include "app/settings_store.hpp"
#include "app/toml.hpp"
#include "render/settings.hpp"
#include "render/units.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

using c3::app::format_settings_toml;
using c3::app::load_settings;
using c3::app::parse_ui_settings;
using c3::app::save_settings;
using c3::app::SettingsLoad;
using c3::app::SettingsStatus;
using c3::render::PressureUnit;
using c3::render::Settings;
using c3::render::TempUnit;
using c3::render::ThemePreference;
using c3::render::Units;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::Message;

CATCH_REGISTER_ENUM(SettingsStatus, SettingsStatus::Loaded, SettingsStatus::Missing, SettingsStatus::Corrupt)
CATCH_REGISTER_ENUM(ThemePreference, ThemePreference::Dark, ThemePreference::Light, ThemePreference::Auto)

namespace {

namespace fs = std::filesystem;

class ScratchDir {
public:
  ScratchDir() {
    static int counter = 0;
    path_ = fs::temp_directory_path() / ("c3_settings_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
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
    std::ofstream(p, std::ios::binary) << content;
    return p.string();
  }
  [[nodiscard]] std::string path_of(std::string_view name) const { return (path_ / name).string(); }
  [[nodiscard]] const fs::path& path() const noexcept { return path_; }

private:
  fs::path path_;
};

[[nodiscard]] std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// Every file in `dir`, for checking that no temp file is left behind.
[[nodiscard]] std::size_t file_count(const fs::path& dir) {
  std::size_t n = 0;
  for (const auto& entry : fs::directory_iterator(dir)) {
    static_cast<void>(entry);
    ++n;
  }
  return n;
}

constexpr Settings kDefaults{};
constexpr Settings kCustom{.theme = ThemePreference::Auto,
                           .accent = 0x2DD4BF,
                           .units = Units{.temp = TempUnit::Celsius, .pressure = PressureUnit::Bar}};

// ---------------------------------------------------------------------------
// parse_ui_settings
// ---------------------------------------------------------------------------

TEST_CASE("parse_ui_settings: an empty document keeps every default", "[settings_store]") {
  CHECK(parse_ui_settings(c3::toml::parse(""), kDefaults) == kDefaults);
  CHECK(parse_ui_settings(c3::toml::parse("[ui]"), kCustom) == kCustom);
  CHECK(parse_ui_settings(c3::toml::parse("[other]\ntheme = \"light\""), kDefaults) == kDefaults);
}

TEST_CASE("parse_ui_settings: a full [ui] table overrides every field", "[settings_store]") {
  const Settings s = parse_ui_settings(
      c3::toml::parse("[ui]\ntheme = \"auto\"\naccent = 0x2DD4BF\ntemperature = \"C\"\npressure = \"bar\"\n"),
      kDefaults);
  CHECK(s == kCustom);
}

TEST_CASE("parse_ui_settings: each key overrides independently", "[settings_store]") {
  CHECK(parse_ui_settings(c3::toml::parse("[ui]\ntheme = \"light\""), kDefaults).theme == ThemePreference::Light);
  CHECK(parse_ui_settings(c3::toml::parse("[ui]\naccent = 255"), kDefaults).accent == 0x0000FF);
  CHECK(parse_ui_settings(c3::toml::parse("[ui]\ntemperature = \"C\""), kDefaults).units.temp == TempUnit::Celsius);
  CHECK(parse_ui_settings(c3::toml::parse("[ui]\npressure = \"bar\""), kDefaults).units.pressure == PressureUnit::Bar);
  // and the untouched fields keep the caller's defaults, not the struct's
  const Settings s = parse_ui_settings(c3::toml::parse("[ui]\ntheme = \"dark\""), kCustom);
  CHECK(s.theme == ThemePreference::Dark);
  CHECK(s.accent == kCustom.accent);
  CHECK(s.units == kCustom.units);
}

TEST_CASE("parse_ui_settings: enumerations are exact and lower case", "[settings_store]") {
  CHECK_THROWS_MATCHES(parse_ui_settings(c3::toml::parse("[ui]\ntheme = \"Dark\""), kDefaults), std::runtime_error,
                       Message("[ui] theme: expected dark|light|auto, got 'Dark'"));
  CHECK_THROWS_MATCHES(parse_ui_settings(c3::toml::parse("[ui]\ntemperature = \"K\""), kDefaults), std::runtime_error,
                       Message("[ui] temperature: expected F|C, got 'K'"));
  CHECK_THROWS_MATCHES(parse_ui_settings(c3::toml::parse("[ui]\npressure = \"kPa\""), kDefaults), std::runtime_error,
                       Message("[ui] pressure: expected psi|bar, got 'kPa'"));
}

TEST_CASE("parse_ui_settings: the accent must be an integer within 24 bits", "[settings_store]") {
  CHECK_THROWS_MATCHES(parse_ui_settings(c3::toml::parse("[ui]\naccent = 0x1000000"), kDefaults), std::runtime_error,
                       Message("[ui] accent: expected 0xRRGGBB"));
  CHECK_THROWS_MATCHES(parse_ui_settings(c3::toml::parse("[ui]\naccent = -1"), kDefaults), std::runtime_error,
                       Message("[ui] accent: expected 0xRRGGBB"));
  CHECK_THROWS_MATCHES(parse_ui_settings(c3::toml::parse("[ui]\naccent = \"#FF6A2B\""), kDefaults), std::runtime_error,
                       Message("[ui] accent: expected integer"));
  CHECK_THROWS_MATCHES(parse_ui_settings(c3::toml::parse("[ui]\naccent = 1.5"), kDefaults), std::runtime_error,
                       Message("[ui] accent: expected integer"));
  CHECK(parse_ui_settings(c3::toml::parse("[ui]\naccent = 0xFFFFFF"), kDefaults).accent == 0xFFFFFF);
  CHECK(parse_ui_settings(c3::toml::parse("[ui]\naccent = 0"), kDefaults).accent == 0);
}

TEST_CASE("parse_ui_settings: a wrongly typed enumeration key reports the type", "[settings_store]") {
  CHECK_THROWS_MATCHES(parse_ui_settings(c3::toml::parse("[ui]\ntheme = 1"), kDefaults), std::runtime_error,
                       Message("[ui] theme: expected string"));
}

// ---------------------------------------------------------------------------
// format_settings_toml
// ---------------------------------------------------------------------------

TEST_CASE("format_settings_toml: writes the [ui] table with the config spellings", "[settings_store]") {
  CHECK(format_settings_toml(kCustom) ==
        "# c3console user settings. Written by the console; safe to edit or delete.\n"
        "[ui]\n"
        "theme = \"auto\"\n"
        "accent = 0x2DD4BF\n"
        "temperature = \"C\"\n"
        "pressure = \"bar\"\n");
  CHECK_THAT(format_settings_toml(kDefaults), ContainsSubstring("theme = \"dark\"\naccent = 0xFF6A2B\n"));
}

TEST_CASE("format_settings_toml: the accent is six hex digits even with leading zeros", "[settings_store]") {
  CHECK_THAT(format_settings_toml(Settings{.accent = 0x00ABCD}), ContainsSubstring("accent = 0x00ABCD\n"));
  CHECK_THAT(format_settings_toml(Settings{.accent = 0}), ContainsSubstring("accent = 0x000000\n"));
}

TEST_CASE("format_settings_toml: output round-trips through parse_ui_settings", "[settings_store]") {
  for (const Settings& s : {kDefaults, kCustom, Settings{.theme = ThemePreference::Light, .accent = 0x00ABCD}}) {
    CHECK(parse_ui_settings(c3::toml::parse(format_settings_toml(s)), Settings{}) == s);
  }
}

// ---------------------------------------------------------------------------
// load_settings
// ---------------------------------------------------------------------------

TEST_CASE("load_settings: a missing file is Missing with the defaults in force", "[settings_store]") {
  const ScratchDir dir;
  const std::string path = dir.path_of("settings.toml");
  const SettingsLoad r = load_settings(path, kCustom);
  CHECK(r.status == SettingsStatus::Missing);
  CHECK(r.settings == kCustom);
  CHECK(r.detail == path + " does not exist");
}

TEST_CASE("load_settings: a good file is Loaded with no detail", "[settings_store]") {
  const ScratchDir dir;
  const std::string path = dir.file("settings.toml", format_settings_toml(kCustom));
  const SettingsLoad r = load_settings(path, kDefaults);
  CHECK(r.status == SettingsStatus::Loaded);
  CHECK(r.settings == kCustom);
  CHECK(r.detail.empty());
}

TEST_CASE("load_settings: a partial file is Loaded over the defaults", "[settings_store]") {
  const ScratchDir dir;
  const std::string path = dir.file("settings.toml", "[ui]\npressure = \"bar\"\n");
  const SettingsLoad r = load_settings(path, kDefaults);
  CHECK(r.status == SettingsStatus::Loaded);
  CHECK(r.settings.units.pressure == PressureUnit::Bar);
  CHECK(r.settings.theme == kDefaults.theme);
  CHECK(r.settings.accent == kDefaults.accent);
}

TEST_CASE("load_settings: a malformed file is Corrupt, keeps the defaults and says where", "[settings_store]") {
  const ScratchDir dir;
  const std::string path = dir.file("settings.toml", "[ui]\ntheme = \n");
  const SettingsLoad r = load_settings(path, kCustom);
  CHECK(r.status == SettingsStatus::Corrupt);
  CHECK(r.settings == kCustom);
  CHECK(r.detail == path + ":line 2: missing value");
  CHECK(fs::exists(path));  // left for inspection
}

TEST_CASE("load_settings: a file with a rejected value is Corrupt with the value's reason", "[settings_store]") {
  const ScratchDir dir;
  const std::string path = dir.file("settings.toml", "[ui]\ntheme = \"sepia\"\n");
  const SettingsLoad r = load_settings(path, kDefaults);
  CHECK(r.status == SettingsStatus::Corrupt);
  CHECK(r.settings == kDefaults);
  CHECK(r.detail == "[ui] theme: expected dark|light|auto, got 'sepia'");
}

TEST_CASE("load_settings: a path that exists but is not a file is Corrupt, not Missing", "[settings_store]") {
  const ScratchDir dir;
  const SettingsLoad r = load_settings(dir.path().string(), kDefaults);  // a directory
  CHECK(r.status == SettingsStatus::Corrupt);
  CHECK(r.settings == kDefaults);
  CHECK(r.detail == "cannot read " + dir.path().string() + ": not a regular file");
}

// ---------------------------------------------------------------------------
// save_settings
// ---------------------------------------------------------------------------

TEST_CASE("save_settings: writes a file that load_settings reads back, leaving no temp file", "[settings_store]") {
  const ScratchDir dir;
  const std::string path = dir.path_of("settings.toml");
  CHECK_FALSE(save_settings(path, kCustom).has_value());
  CHECK(read_file(path) == format_settings_toml(kCustom));
  CHECK(file_count(dir.path()) == 1);
  CHECK_FALSE(fs::exists(path + ".tmp"));

  const SettingsLoad r = load_settings(path, kDefaults);
  CHECK(r.status == SettingsStatus::Loaded);
  CHECK(r.settings == kCustom);
}

TEST_CASE("save_settings: replaces an existing file in place", "[settings_store]") {
  const ScratchDir dir;
  const std::string path = dir.file("settings.toml", "garbage that is not toml");
  CHECK_FALSE(save_settings(path, kDefaults).has_value());
  CHECK(read_file(path) == format_settings_toml(kDefaults));
  CHECK_FALSE(save_settings(path, kCustom).has_value());
  CHECK(read_file(path) == format_settings_toml(kCustom));
  CHECK(file_count(dir.path()) == 1);
}

TEST_CASE("save_settings: a path in a directory that does not exist fails at the temp file", "[settings_store]") {
  const ScratchDir dir;
  const std::string path = dir.path_of("nope/settings.toml");
  const auto err = save_settings(path, kDefaults);
  REQUIRE(err.has_value());
  CHECK_THAT(*err, ContainsSubstring("cannot create " + path + ".tmp"));
  CHECK_FALSE(fs::exists(path));
}

TEST_CASE("save_settings: a target that cannot be replaced fails after the write and cleans up", "[settings_store]") {
  const ScratchDir dir;
  const fs::path blocker = dir.path() / "settings.toml";
  fs::create_directories(blocker / "child");  // a non-empty directory: rename() over it fails
  const auto err = save_settings(blocker.string(), kDefaults);
  REQUIRE(err.has_value());
  CHECK_THAT(*err, ContainsSubstring("cannot replace " + blocker.string()));
  CHECK(fs::is_directory(blocker));  // the previous entry is untouched
  CHECK_FALSE(fs::exists(blocker.string() + ".tmp"));
}

TEST_CASE("save_settings: a relative path in the working directory is written and synced", "[settings_store]") {
  // The directory fsync falls back to "." when the path has no parent.
  const std::string name = "c3_settings_test_" + std::to_string(::getpid()) + ".toml";
  CHECK_FALSE(save_settings(name, kDefaults).has_value());
  CHECK(fs::exists(name));
  std::error_code ec;
  fs::remove(name, ec);
}

}  // namespace
