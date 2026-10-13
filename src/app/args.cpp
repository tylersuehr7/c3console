#include "args.hpp"

#include <array>
#include <string_view>

namespace c3::app {
namespace {

struct ValueFlag {
  std::string_view name;
  std::optional<std::string> Args::* target;
};

constexpr std::array<ValueFlag, 9> kValueFlags = {{
    {"--source", &Args::source},
    {"--profile", &Args::profile},
    {"--replay", &Args::replay},
    {"--config", &Args::config},
    {"--headless", &Args::headless},
    {"--screenshot-after", &Args::screenshot_after},
    {"--screenshot", &Args::screenshot},
    {"--open", &Args::open},
    {"--record", &Args::record},
}};

enum class Match {
  NotThisFlag,
  Value,
  MissingValue,
};

struct FlagMatch {
  Match kind = Match::NotThisFlag;
  std::string value;
};

// Recognises `--name=value` and `--name value`. Advances `i` past a separate value token.
[[nodiscard]] FlagMatch match_value_flag(const std::vector<std::string>& argv, std::size_t& i, std::string_view name) {
  const std::string_view tok = argv[i];

  if (tok == name) {
    const bool has_value = i + 1 < argv.size() && !argv[i + 1].starts_with("--");
    if (!has_value) {
      return {Match::MissingValue, {}};
    }
    return {Match::Value, argv[++i]};
  }

  if (tok.size() > name.size() && tok.starts_with(name) && tok[name.size()] == '=') {
    std::string value(tok.substr(name.size() + 1));
    if (value.empty()) {
      return {Match::MissingValue, {}};
    }
    return {Match::Value, std::move(value)};
  }

  return {};
}

}  // namespace

ArgsResult parse_args(const std::vector<std::string>& argv) {
  ArgsResult result;
  Args& a = result.args;

  for (std::size_t i = 1; i < argv.size(); ++i) {
    const std::string& tok = argv[i];

    if (tok == "--help" || tok == "-h") {
      a.help = true;
      continue;
    }

    if (tok == "--desktop") {
      a.desktop = true;
      continue;
    }

    bool matched = false;
    for (const ValueFlag& flag : kValueFlags) {
      FlagMatch m = match_value_flag(argv, i, flag.name);

      if (m.kind == Match::NotThisFlag) {
        continue;
      }

      if (m.kind == Match::MissingValue) {
        result.error = std::string(flag.name) + " requires a value";
        return result;
      }

      a.*flag.target = std::move(m.value);
      matched = true;
      break;
    }

    if (!matched) {
      result.error = "unknown argument '" + tok + "'";
      return result;
    }
  }

  return result;
}

std::string usage(const std::string& program) {
  return "usage: " + program +
         " [--desktop] [--source=mock|replay|uart] [--profile=NAME]\n"
         "       [--replay=FILE] [--config=PATH] [--headless=SECONDS]\n"
         "       [--screenshot=PATH] [--help]\n"
         "\n"
         "  --desktop        SDL2 desktop backend (development default)\n"
         "  --source=SRC     telemetry source: mock | replay | uart\n"
         "  --profile=NAME   MockSource profile, e.g. WarmupDrive, ColdStart, Overheat\n"
         "  --replay=FILE    CSV to play back (implies --source=replay)\n"
         "  --config=PATH    TOML config (default config/console.toml)\n"
         "  --headless=SEC   run the source without a display, log 1 line/s (0 = until Ctrl-C)\n"
         "  --screenshot=P   desktop: run, write PATH as PPM, exit\n"
         "  --screenshot-after=S  seconds before the capture (default 0.5)\n"
         "  --open=SURFACE   desktop: start with "
         "camera|settings|audio|theme|units|calibration|system\n"
         "  --record=PATH    write every accepted frame to PATH as CSV (spec 9.3)\n";
}

}  // namespace c3::app
