// args.cpp — command-line flag recognition. Values are not interpreted here; see test_config.
#include "app/args.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <initializer_list>
#include <string>
#include <vector>

using c3::app::Args;
using c3::app::ArgsResult;
using c3::app::parse_args;
using c3::app::usage;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;

namespace {

/// Parses the tokens after the program name, as main() would pass them.
[[nodiscard]] ArgsResult parse(std::initializer_list<const char*> tokens) {
  std::vector<std::string> argv = {"c3console"};
  for (const char* t : tokens) {
    argv.emplace_back(t);
  }
  return parse_args(argv);
}

/// Parses and requires success.
[[nodiscard]] Args ok(std::initializer_list<const char*> tokens) {
  const ArgsResult r = parse(tokens);
  INFO(r.error.value_or("(no error)"));
  REQUIRE_FALSE(r.error.has_value());
  return r.args;
}

/// Parses and requires failure; returns the message.
[[nodiscard]] std::string error_of(std::initializer_list<const char*> tokens) {
  const ArgsResult r = parse(tokens);
  REQUIRE(r.error.has_value());
  return *r.error;
}

TEST_CASE("parse_args: no arguments is the default, error-free configuration", "[args]") {
  const Args a = ok({});
  CHECK_FALSE(a.help);
  CHECK_FALSE(a.desktop);
  CHECK_FALSE(a.source.has_value());
  CHECK_FALSE(a.profile.has_value());
  CHECK_FALSE(a.replay.has_value());
  CHECK_FALSE(a.config.has_value());
  CHECK_FALSE(a.headless.has_value());
  CHECK_FALSE(a.screenshot.has_value());
  CHECK_FALSE(a.screenshot_after.has_value());
  CHECK_FALSE(a.open.has_value());
  CHECK_FALSE(a.record.has_value());
}

TEST_CASE("parse_args: the program name alone is skipped and an empty argv is tolerated", "[args]") {
  CHECK_FALSE(parse_args({}).error.has_value());
  CHECK_FALSE(parse_args({"/usr/bin/c3console"}).error.has_value());
}

TEST_CASE("parse_args: --help and -h set help wherever they appear", "[args]") {
  CHECK(ok({"--help"}).help);
  CHECK(ok({"-h"}).help);
  const Args a = ok({"--desktop", "--help", "--source=mock"});
  CHECK(a.help);
  CHECK(a.desktop);
  CHECK(a.source == "mock");
}

TEST_CASE("parse_args: --desktop is a bare switch", "[args]") {
  CHECK(ok({"--desktop"}).desktop);
  CHECK(error_of({"--desktop=1"}) == "unknown argument '--desktop=1'");
}

TEST_CASE("parse_args: every value flag accepts --flag=value and --flag value", "[args]") {
  struct Case {
    const char* flag;
    std::optional<std::string> Args::* field;
  };
  constexpr Case kCases[] = {
      {"--source", &Args::source},
      {"--profile", &Args::profile},
      {"--replay", &Args::replay},
      {"--config", &Args::config},
      {"--headless", &Args::headless},
      {"--screenshot", &Args::screenshot},
      {"--screenshot-after", &Args::screenshot_after},
      {"--open", &Args::open},
      {"--record", &Args::record},
  };
  for (const Case& c : kCases) {
    INFO(c.flag);
    const std::string joined = std::string(c.flag) + "=value1";
    const Args equals = ok({joined.c_str()});
    CHECK(equals.*c.field == "value1");
    const Args separate = ok({c.flag, "value2"});
    CHECK(separate.*c.field == "value2");
    CHECK_FALSE(equals.help);
    CHECK_FALSE(equals.desktop);
  }
}

TEST_CASE("parse_args: a repeated flag keeps its last value", "[args]") {
  const Args a = ok({"--source=mock", "--source", "uart", "--source=replay"});
  CHECK(a.source == "replay");
}

TEST_CASE("parse_args: a value flag at the end or followed by another flag is missing its value", "[args]") {
  CHECK(error_of({"--source"}) == "--source requires a value");
  CHECK(error_of({"--source", "--desktop"}) == "--source requires a value");
  CHECK(error_of({"--replay", "--source=mock"}) == "--replay requires a value");
  CHECK(error_of({"--source="}) == "--source requires a value");
}

TEST_CASE("parse_args: a separate value may start with a single dash or be a bare word", "[args]") {
  // "-5" is not a flag, so it is taken as the value; resolve_config() rejects it later.
  CHECK(ok({"--headless", "-5"}).headless == "-5");
  CHECK(ok({"--config", "console.toml"}).config == "console.toml");
  CHECK(ok({"--replay", "drive.csv", "--desktop"}).replay == "drive.csv");
}

TEST_CASE("parse_args: values keep everything after the first equals sign", "[args]") {
  CHECK(ok({"--config=/tmp/a=b.toml"}).config == "/tmp/a=b.toml");
  CHECK(ok({"--open=settings "}).open == "settings ");  // untrimmed: validation decides
}

TEST_CASE("parse_args: --screenshot and --screenshot-after do not shadow each other", "[args]") {
  const Args a = ok({"--screenshot-after=1.5", "--screenshot=/tmp/panel.ppm"});
  CHECK(a.screenshot_after == "1.5");
  CHECK(a.screenshot == "/tmp/panel.ppm");
  const Args b = ok({"--screenshot", "/tmp/p.ppm", "--screenshot-after", "2"});
  CHECK(b.screenshot == "/tmp/p.ppm");
  CHECK(b.screenshot_after == "2");
}

TEST_CASE("parse_args: unknown flags, prefixes and positional words are errors", "[args]") {
  CHECK(error_of({"--bogus"}) == "unknown argument '--bogus'");
  CHECK(error_of({"--sourc=mock"}) == "unknown argument '--sourc=mock'");
  CHECK(error_of({"--sources=mock"}) == "unknown argument '--sources=mock'");
  CHECK(error_of({"-d"}) == "unknown argument '-d'");
  CHECK(error_of({"drive.csv"}) == "unknown argument 'drive.csv'");
  CHECK(error_of({"--desktop", "extra"}) == "unknown argument 'extra'");
}

TEST_CASE("parse_args: parsing stops at the first error", "[args]") {
  const ArgsResult r = parse({"--source=mock", "--nope", "--replay=x.csv"});
  REQUIRE(r.error.has_value());
  CHECK(*r.error == "unknown argument '--nope'");
}

TEST_CASE("parse_args: a full development command line round-trips every field", "[args]") {
  const Args a =
      ok({"--desktop", "--source=mock", "--profile", "Overheat", "--config=config/dev.toml", "--headless=30",
          "--screenshot=/tmp/p.ppm", "--screenshot-after=0.25", "--open=calibration", "--record", "/tmp/drive.csv"});
  CHECK(a.desktop);
  CHECK(a.source == "mock");
  CHECK(a.profile == "Overheat");
  CHECK(a.config == "config/dev.toml");
  CHECK(a.headless == "30");
  CHECK(a.screenshot == "/tmp/p.ppm");
  CHECK(a.screenshot_after == "0.25");
  CHECK(a.open == "calibration");
  CHECK(a.record == "/tmp/drive.csv");
  CHECK_FALSE(a.replay.has_value());
  CHECK_FALSE(a.help);
}

TEST_CASE("usage: names the program and every flag", "[args]") {
  const std::string text = usage("c3console");
  CHECK_THAT(text, StartsWith("usage: c3console "));
  for (const char* flag : {"--desktop", "--source=", "--profile=", "--replay=", "--config=", "--headless=",
                           "--screenshot=", "--screenshot-after=", "--open=", "--record=", "--help"}) {
    INFO(flag);
    CHECK_THAT(text, ContainsSubstring(flag));
  }
  CHECK_THAT(text, ContainsSubstring("mock | replay | uart"));
  CHECK(text.back() == '\n');
}

}  // namespace
