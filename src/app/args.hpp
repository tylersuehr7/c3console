// Command-line flags. Only what the build spec names; everything else lives in console.toml.
//
//   --desktop            use the SDL2 desktop backend (equivalent to backend = "desktop")
//   --source=mock|replay|uart
//   --profile=NAME       MockSource profile (ColdStart, WarmupDrive, HotIdle, HighwayCruise,
//                        OilPressureLoss, Overheat, SenderOpen, AlternatorFail)
//   --replay=FILE        CSV to play back; implies --source=replay
//   --config=PATH        TOML file (default: config/console.toml, searched from the working
//                        directory and then relative to the executable)
//   --headless=SECONDS   run the telemetry source without a display and log a summary line
//                        each second; 0 runs until Ctrl-C. Development aid, not in the spec.
//   --screenshot=PATH    desktop only: render for --screenshot-after seconds, write the panel to
//                        PATH as a binary PPM, exit. Verification aid, not in the spec.
//   --screenshot-after=S seconds to run before capturing (default 0.5)
//   --record=PATH        append every accepted frame to PATH as CSV (spec §9.3); works with
//                        any source, including --headless
//   --open=SURFACE       desktop only: start with audio|settings|calibration|camera showing in
//                        the well, for screenshots. Verification aid, not in the spec.
//   --help, -h
//
// Both `--flag=value` and `--flag value` are accepted; a value may not begin with "--". A
// repeated flag keeps its last value. Values are strings here: parse_args() only recognises
// the flags, and resolve_config() interprets and validates them against the file.
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace c3::app {

struct Args {
  bool help = false;
  bool desktop = false;
  std::optional<std::string> source;
  std::optional<std::string> profile;
  std::optional<std::string> replay;
  std::optional<std::string> config;
  std::optional<std::string> headless;  // seconds, validated in resolve_config()
  std::optional<std::string> screenshot;
  std::optional<std::string> screenshot_after;  // seconds, validated in resolve_config()
  std::optional<std::string> open;              // surface name, validated in resolve_config()
  std::optional<std::string> record;            // CSV path
};

struct ArgsResult {
  Args args;
  std::optional<std::string> error;  // set when parsing failed; args is then unspecified
};

/// Parses `argv` as main() receives it: element 0 is the program and is skipped. Stops at the
/// first unknown argument or flag without a value and reports it in `error`.
[[nodiscard]] ArgsResult parse_args(const std::vector<std::string>& argv);

/// The help text, starting "usage: <program> ...".
[[nodiscard]] std::string usage(const std::string& program);

}  // namespace c3::app
