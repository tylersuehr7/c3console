#include "settings_store.hpp"

#include "utils/fixed_format.hpp"
#include "utils/parse_enum_name.hpp"
#include "utils/unique_fd.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <system_error>

namespace c3::app {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string errno_text() {
  return std::system_category().message(errno);
}

// Writes all of `text` to `fd`; false on the first failure.
[[nodiscard]] bool write_all(int fd, std::string_view text) noexcept {
  while (!text.empty()) {
    const ssize_t n = ::write(fd, text.data(), text.size());
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    text.remove_prefix(static_cast<std::size_t>(n));
  }
  return true;
}

}  // namespace

render::Settings parse_ui_settings(const toml::Document& doc, const render::Settings& defaults) {
  render::Settings s = defaults;
  const std::string theme = toml::get_string(doc, "ui", "theme", std::string(to_string(s.theme)));
  const auto parsed_theme = utils::parse_enum(theme, render::kAllThemePreferences);
  if (!parsed_theme) {
    throw std::runtime_error("[ui] theme: expected dark|light|auto, got '" + theme + "'");
  }

  s.theme = *parsed_theme;
  const std::int64_t accent = toml::get_int(doc, "ui", "accent", s.accent);
  if (accent < 0 || accent > 0xFFFFFF) {
    throw std::runtime_error("[ui] accent: expected 0xRRGGBB");
  }

  s.accent = static_cast<std::uint32_t>(accent);
  const std::string temp = toml::get_string(doc, "ui", "temperature", std::string(to_string(s.units.temp)));
  const auto parsed_temp = utils::parse_enum(temp, render::kAllTempUnits);
  if (!parsed_temp) {
    throw std::runtime_error("[ui] temperature: expected F|C, got '" + temp + "'");
  }

  s.units.temp = *parsed_temp;
  const std::string pressure = toml::get_string(doc, "ui", "pressure", std::string(to_string(s.units.pressure)));
  const auto parsed_pressure = utils::parse_enum(pressure, render::kAllPressureUnits);
  if (!parsed_pressure) {
    throw std::runtime_error("[ui] pressure: expected psi|bar, got '" + pressure + "'");
  }

  s.units.pressure = *parsed_pressure;
  return s;
}

std::string format_settings_toml(const render::Settings& s) {
  std::array<char, 16> accent{};
  std::string out = "# c3console user settings. Written by the console; safe to edit or delete.\n";
  out += "[ui]\n";
  out += "theme = \"" + std::string(to_string(s.theme)) + "\"\n";
  out += "accent = " + std::string(utils::format_into(accent, "0x%06X", s.accent)) + "\n";
  out += "temperature = \"" + std::string(to_string(s.units.temp)) + "\"\n";
  out += "pressure = \"" + std::string(to_string(s.units.pressure)) + "\"\n";
  return out;
}

SettingsLoad load_settings(const std::string& path, const render::Settings& defaults) {
  std::error_code ec;
  if (!fs::exists(path, ec)) {
    return {.settings = defaults, .status = SettingsStatus::Missing, .detail = path + " does not exist"};
  }

  try {
    const toml::Document doc = toml::parse_file(path);
    return {.settings = parse_ui_settings(doc, defaults), .status = SettingsStatus::Loaded, .detail = {}};
  } catch (const std::exception& e) {
    return {.settings = defaults, .status = SettingsStatus::Corrupt, .detail = e.what()};
  }
}

std::optional<std::string> save_settings(const std::string& path, const render::Settings& s) {
  const fs::path target(path);
  const fs::path tmp = target.string() + ".tmp";
  const std::string text = format_settings_toml(s);

  {
    utils::UniqueFd fd(::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644));

    if (!fd) {
      return "cannot create " + tmp.string() + ": " + errno_text();
    }

    if (!write_all(fd.get(), text) || ::fsync(fd.get()) != 0) {
      const std::string why = errno_text();
      static_cast<void>(::unlink(tmp.c_str()));
      return "cannot write " + tmp.string() + ": " + why;
    }
  }

  if (::rename(tmp.c_str(), target.c_str()) != 0) {
    const std::string why = errno_text();
    static_cast<void>(::unlink(tmp.c_str()));
    return "cannot replace " + path + ": " + why;
  }

  // The rename is durable only once the directory entry is flushed.
  const fs::path dir = target.has_parent_path() ? target.parent_path() : fs::path(".");
  const utils::UniqueFd dfd(::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  if (!dfd || ::fsync(dfd.get()) != 0) {
    return "cannot sync settings directory: " + errno_text();
  }

  return std::nullopt;
}

}  // namespace c3::app
