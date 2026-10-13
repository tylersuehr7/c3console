// Settings persistence — the console's user preferences on the writable data partition.
//
// The file is a TOML `[ui]` table with the same keys as config/console.toml, so a user can
// read it and the two never disagree on spelling. Writes are atomic: temp file, fsync, rename,
// directory fsync, so a power cut leaves either the old file or the new one. A missing or
// corrupt file falls back to the defaults from console.toml and says so; it never stops the
// console from starting. Init-time and on a settings tap only; never on the frame path.
#pragma once

#include "render/settings.hpp"
#include "toml.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace c3::app {

/// Reads the `[ui]` table over `defaults`; a key that is absent keeps the default. Throws
/// std::runtime_error on a value that does not parse (shared with config loading).
[[nodiscard]] render::Settings parse_ui_settings(const toml::Document& doc, const render::Settings& defaults);

/// The `[ui]` table for `s`, ready to write.
[[nodiscard]] std::string format_settings_toml(const render::Settings& s);

enum class SettingsStatus : std::uint8_t {
  Loaded,   // file read and applied
  Missing,  // no file yet: defaults in force
  Corrupt,  // file unreadable or a value rejected: defaults in force, file left for inspection
};

struct SettingsLoad {
  render::Settings settings;
  SettingsStatus status = SettingsStatus::Missing;
  std::string detail;  // why, for the log; empty when Loaded
};

/// Reads `path` over `defaults`. Never throws: the status says whether the result is the file's
/// content or the defaults, and `detail` says why.
[[nodiscard]] SettingsLoad load_settings(const std::string& path, const render::Settings& defaults);

/// Atomic replace of `path` via `path.tmp`, fsync and rename. Returns the failure reason, or
/// nullopt on success; on failure the previous file, if any, is untouched.
[[nodiscard]] std::optional<std::string> save_settings(const std::string& path, const render::Settings& s);

}  // namespace c3::app
