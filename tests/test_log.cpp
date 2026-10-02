#include "logging/log.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace l = c3::logging;

namespace {

/// Restores the global level when the case ends so cases cannot leak level into one another.
class ScopedLevel {
public:
  explicit ScopedLevel(l::Level level) noexcept : previous_(l::get_level()) { l::set_level(level); }
  ~ScopedLevel() { l::set_level(previous_); }
  ScopedLevel(const ScopedLevel&) = delete;
  ScopedLevel& operator=(const ScopedLevel&) = delete;
  ScopedLevel(ScopedLevel&&) = delete;
  ScopedLevel& operator=(ScopedLevel&&) = delete;

private:
  l::Level previous_;
};

/// Captures emitted lines so write() can be asserted on without touching stderr.
struct Capture {
  std::vector<std::string> lines;

  static void emit(void* context, std::string_view line) noexcept {
    static_cast<Capture*>(context)->lines.emplace_back(line);
  }

  [[nodiscard]] l::Sink sink() noexcept { return l::Sink{.context = this, .emit = &Capture::emit}; }
};

// ---------------------------------------------------------------------------
// Level
// ---------------------------------------------------------------------------

TEST_CASE("Level: tests that levels order from Debug up to Error", "[log]") {
  CHECK(l::Level::Debug < l::Level::Info);
  CHECK(l::Level::Info < l::Level::Warn);
  CHECK(l::Level::Warn < l::Level::Error);
}

TEST_CASE("set_level/get_level: tests that the level round-trips", "[log]") {
  const ScopedLevel guard(l::Level::Debug);
  l::set_level(l::Level::Warn);
  CHECK(l::get_level() == l::Level::Warn);
  l::set_level(l::Level::Error);
  CHECK(l::get_level() == l::Level::Error);
}

// ---------------------------------------------------------------------------
// to_string
// ---------------------------------------------------------------------------

TEST_CASE("to_string: tests that Level::Debug returns 'debug'", "[log]") {
  const auto level = l::Level::Debug;
  const auto result = l::to_string(level);
  CHECK("debug" == result);
}

TEST_CASE("to_string: tests that Level::Info returns 'info'", "[log]") {
  const auto level = l::Level::Info;
  const auto result = l::to_string(level);
  CHECK("info" == result);
}

TEST_CASE("to_string: tests that Level::Warn returns 'warn'", "[log]") {
  const auto level = l::Level::Warn;
  const auto result = l::to_string(level);
  CHECK("warn" == result);
}

TEST_CASE("to_string: tests that Level::Error returns 'error'", "[log]") {
  const auto level = l::Level::Error;
  const auto result = l::to_string(level);
  CHECK("error" == result);
}

// ---------------------------------------------------------------------------
// to_priority_prefix
// ---------------------------------------------------------------------------

TEST_CASE("to_priority_prefix: tests that Level::Debug returns '<7>'", "[log]") {
  const auto level = l::Level::Debug;
  const auto result = l::to_priority_prefix(level);
  CHECK("<7>" == result);
}

TEST_CASE("to_priority_prefix: tests that Level::Info returns '<6>'", "[log]") {
  const auto level = l::Level::Info;
  const auto result = l::to_priority_prefix(level);
  CHECK("<6>" == result);
}

TEST_CASE("to_priority_prefix: tests that Level::Warn returns '<4>'", "[log]") {
  const auto level = l::Level::Warn;
  const auto result = l::to_priority_prefix(level);
  CHECK("<4>" == result);
}

TEST_CASE("to_priority_prefix: tests that Level::Error returns '<3>'", "[log]") {
  const auto level = l::Level::Error;
  const auto result = l::to_priority_prefix(level);
  CHECK("<3>" == result);
}

// ---------------------------------------------------------------------------
// to_ascii_color
// ---------------------------------------------------------------------------

TEST_CASE("to_ascii_color: tests that Level::Debug returns default foreground '39'", "[log]") {
  CHECK(std::string_view(l::to_ascii_color(l::Level::Debug)) == "39");
}

TEST_CASE("to_ascii_color: tests that Level::Info returns blue '34'", "[log]") {
  CHECK(std::string_view(l::to_ascii_color(l::Level::Info)) == "34");
}

TEST_CASE("to_ascii_color: tests that Level::Warn returns yellow '33'", "[log]") {
  CHECK(std::string_view(l::to_ascii_color(l::Level::Warn)) == "33");
}

TEST_CASE("to_ascii_color: tests that Level::Error returns red '31'", "[log]") {
  CHECK(std::string_view(l::to_ascii_color(l::Level::Error)) == "31");
}

// ---------------------------------------------------------------------------
// write: level filtering
// ---------------------------------------------------------------------------

TEST_CASE("write: tests that no write occurs when below current level", "[log]") {
  Capture capture;
  const l::ScopedSink sink(capture.sink());
  const ScopedLevel guard(l::Level::Info);

  const auto result = l::write(l::Level::Debug, "test", "Something");
  CHECK(false == result);
  CHECK(capture.lines.empty());
}

TEST_CASE("write: tests that write occurs when at current level", "[log]") {
  Capture capture;
  const l::ScopedSink sink(capture.sink());
  const ScopedLevel guard(l::Level::Info);

  const auto result = l::write(l::Level::Info, "test", "Something");
  CHECK(true == result);
  CHECK(capture.lines.size() == 1);
}

TEST_CASE("write: tests that write occurs when above current level", "[log]") {
  Capture capture;
  const l::ScopedSink sink(capture.sink());
  const ScopedLevel guard(l::Level::Info);

  const auto result = l::write(l::Level::Warn, "test", "Something");
  CHECK(true == result);
  CHECK(capture.lines.size() == 1);
}

TEST_CASE("write: tests that only Error passes when current level is Error", "[log]") {
  Capture capture;
  const l::ScopedSink sink(capture.sink());
  const ScopedLevel guard(l::Level::Error);

  CHECK(false == l::write(l::Level::Debug, "test", "Something"));
  CHECK(false == l::write(l::Level::Info, "test", "Something"));
  CHECK(false == l::write(l::Level::Warn, "test", "Something"));
  CHECK(true == l::write(l::Level::Error, "test", "Something"));
  CHECK(capture.lines.size() == 1);
}

TEST_CASE("write: tests that every level passes when current level is Debug", "[log]") {
  Capture capture;
  const l::ScopedSink sink(capture.sink());
  const ScopedLevel guard(l::Level::Debug);

  CHECK(true == l::write(l::Level::Debug, "test", "Something"));
  CHECK(true == l::write(l::Level::Info, "test", "Something"));
  CHECK(true == l::write(l::Level::Warn, "test", "Something"));
  CHECK(true == l::write(l::Level::Error, "test", "Something"));
  CHECK(capture.lines.size() == 4);
}

// ---------------------------------------------------------------------------
// write: line format
// ---------------------------------------------------------------------------

TEST_CASE("write: tests that the emitted line carries journald prefix, color, level, tag and message", "[log]") {
  Capture capture;
  const l::ScopedSink sink(capture.sink());
  const ScopedLevel guard(l::Level::Debug);

  CHECK(l::write(l::Level::Info, "uart", "port opened"));
  REQUIRE(capture.lines.size() == 1);
  CHECK(capture.lines[0] == "<6>\033[0;34m[info] (uart) port opened\033[0m\n");
}

TEST_CASE("write: tests that Error lines use the error prefix and red color", "[log]") {
  Capture capture;
  const l::ScopedSink sink(capture.sink());
  const ScopedLevel guard(l::Level::Debug);

  CHECK(l::write(l::Level::Error, "hvac", "relay fault"));
  REQUIRE(capture.lines.size() == 1);
  CHECK(capture.lines[0] == "<3>\033[0;31m[error] (hvac) relay fault\033[0m\n");
}

TEST_CASE("write: tests that empty tag and message still produce a well-formed line", "[log]") {
  Capture capture;
  const l::ScopedSink sink(capture.sink());
  const ScopedLevel guard(l::Level::Debug);

  CHECK(l::write(l::Level::Warn, "", ""));
  REQUIRE(capture.lines.size() == 1);
  CHECK(capture.lines[0] == "<4>\033[0;33m[warn] () \033[0m\n");
}

TEST_CASE("write: tests that an oversized message is truncated to the fixed line buffer and still reported written",
          "[log]") {
  Capture capture;
  const l::ScopedSink sink(capture.sink());
  const ScopedLevel guard(l::Level::Debug);

  const std::string message(2000, 'x');
  CHECK(l::write(l::Level::Info, "test", message));
  REQUIRE(capture.lines.size() == 1);
  CHECK(capture.lines[0].size() == 1024);
  CHECK(capture.lines[0].starts_with("<6>\033[0;34m[info] (test) xxxx"));
}

// ---------------------------------------------------------------------------
// debug / info / warn / error wrappers
// ---------------------------------------------------------------------------

TEST_CASE("debug: tests that wrapper writes at Level::Debug", "[log]") {
  Capture capture;
  const l::ScopedSink sink(capture.sink());
  const ScopedLevel guard(l::Level::Debug);

  l::debug("t", "m");
  REQUIRE(capture.lines.size() == 1);
  CHECK(capture.lines[0].starts_with("<7>"));
}

TEST_CASE("info: tests that wrapper writes at Level::Info", "[log]") {
  Capture capture;
  const l::ScopedSink sink(capture.sink());
  const ScopedLevel guard(l::Level::Debug);

  l::info("t", "m");
  REQUIRE(capture.lines.size() == 1);
  CHECK(capture.lines[0].starts_with("<6>"));
}

TEST_CASE("warn: tests that wrapper writes at Level::Warn", "[log]") {
  Capture capture;
  const l::ScopedSink sink(capture.sink());
  const ScopedLevel guard(l::Level::Debug);

  l::warn("t", "m");
  REQUIRE(capture.lines.size() == 1);
  CHECK(capture.lines[0].starts_with("<4>"));
}

TEST_CASE("error: tests that wrapper writes at Level::Error", "[log]") {
  Capture capture;
  const l::ScopedSink sink(capture.sink());
  const ScopedLevel guard(l::Level::Debug);

  l::error("t", "m");
  REQUIRE(capture.lines.size() == 1);
  CHECK(capture.lines[0].starts_with("<3>"));
}

TEST_CASE("debug: tests that wrapper is filtered when current level is higher", "[log]") {
  Capture capture;
  const l::ScopedSink sink(capture.sink());
  const ScopedLevel guard(l::Level::Info);

  l::debug("t", "m");
  CHECK(capture.lines.empty());
}

// ---------------------------------------------------------------------------
// Sink / ScopedSink
// ---------------------------------------------------------------------------

TEST_CASE("set_sink: tests that the previously installed sink is returned", "[log]") {
  Capture first;
  Capture second;
  const l::ScopedSink outer(first.sink());

  const auto previous = l::set_sink(second.sink());
  CHECK(previous.context == &first);
  CHECK(previous.emit == &Capture::emit);

  static_cast<void>(l::set_sink(previous));
}

TEST_CASE("set_sink: tests that the sink receives its own context pointer", "[log]") {
  Capture capture;
  const l::ScopedSink sink(capture.sink());
  const ScopedLevel guard(l::Level::Debug);

  // Capture::emit dereferences the context as a Capture*; a wrong pointer would not land here.
  CHECK(l::write(l::Level::Info, "t", "m"));
  CHECK(capture.lines.size() == 1);
}

TEST_CASE("ScopedSink: tests that the previous sink is restored when the scope ends", "[log]") {
  Capture outer_capture;
  Capture inner_capture;
  const ScopedLevel guard(l::Level::Debug);
  const l::ScopedSink outer(outer_capture.sink());

  CHECK(l::write(l::Level::Info, "t", "before"));
  {
    const l::ScopedSink inner(inner_capture.sink());
    CHECK(l::write(l::Level::Info, "t", "inside"));
  }
  CHECK(l::write(l::Level::Info, "t", "after"));

  REQUIRE(outer_capture.lines.size() == 2);
  CHECK(outer_capture.lines[0].ends_with("before\033[0m\n"));
  CHECK(outer_capture.lines[1].ends_with("after\033[0m\n"));
  REQUIRE(inner_capture.lines.size() == 1);
  CHECK(inner_capture.lines[0].ends_with("inside\033[0m\n"));
}

}  // namespace
