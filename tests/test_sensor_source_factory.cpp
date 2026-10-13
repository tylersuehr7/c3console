// sensor_source_factory.cpp — the Config becomes a mock, a replay or a UART source.
#include "app/config.hpp"
#include "app/sensor_source_factory.hpp"
#include "logging/log.hpp"
#include "telemetry/csv_codec.hpp"
#include "telemetry/mock_source.hpp"
#include "telemetry/replay_source.hpp"
#include "telemetry/sensor_source.hpp"
#include "telemetry/uart_source.hpp"
#include "vehicle/vehicle_commands.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <unistd.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

using c3::app::Config;
using c3::app::make_sensor_source;
using c3::app::Source;
using c3::app::SourceResult;
using c3::telemetry::LinkState;
using c3::telemetry::MockProfile;
using c3::telemetry::MockSource;
using c3::telemetry::ReplaySource;
using c3::telemetry::UartSource;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;

namespace {

namespace fs = std::filesystem;

std::uint64_t g_now_ms = 0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

[[nodiscard]] std::uint64_t fake_now() noexcept {
  return g_now_ms;
}

class ScratchDir {
public:
  ScratchDir() {
    static int counter = 0;
    path_ = fs::temp_directory_path() / ("c3_factory_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
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

private:
  fs::path path_;
};

/// Captures log lines so the replay summary can be checked and the output stays quiet.
class LogCapture {
public:
  LogCapture() : previous_(c3::logging::get_level()), sink_({.context = this, .emit = &LogCapture::emit}) {
    c3::logging::set_level(c3::logging::Level::Debug);
  }
  ~LogCapture() { c3::logging::set_level(previous_); }
  LogCapture(const LogCapture&) = delete;
  LogCapture& operator=(const LogCapture&) = delete;
  LogCapture(LogCapture&&) = delete;
  LogCapture& operator=(LogCapture&&) = delete;

  std::vector<std::string> lines;

  [[nodiscard]] bool any_contains(std::string_view text) const {
    for (const std::string& line : lines) {
      if (line.find(text) != std::string::npos) {
        return true;
      }
    }
    return false;
  }

private:
  static void emit(void* context, std::string_view line) noexcept {
    static_cast<LogCapture*>(context)->lines.emplace_back(line);
  }
  c3::logging::Level previous_;
  c3::logging::ScopedSink sink_;
};

/// A three-row recording in CsvRecorder's format, one frame per second.
[[nodiscard]] std::string recording(std::string_view extra_row = {}) {
  constexpr std::string_view kBody = ",12843,8211,19004,21877,0.6200,41.20,196.40,14.100,";
  std::string out(c3::telemetry::kCsvHeader);
  out += "\n";
  for (const std::uint32_t t : {0U, 1000U, 2000U}) {
    out += std::to_string(t) + std::string(kBody) + "\n";
  }
  if (!extra_row.empty()) {
    out += std::string(extra_row) + "\n";
  }
  return out;
}

/// Opens a pseudo-terminal and returns the slave's device path; the master is kept open so the
/// line stays usable.
struct Pty {
  int master = -1;
  std::array<char, 128> name{};
  Pty() {
    int slave = -1;
    REQUIRE(::openpty(&master, &slave, name.data(), nullptr, nullptr) == 0);
    static_cast<void>(::close(slave));  // UartSource::open reopens it by name
  }
  ~Pty() { static_cast<void>(::close(master)); }
  Pty(const Pty&) = delete;
  Pty& operator=(const Pty&) = delete;
  Pty(Pty&&) = delete;
  Pty& operator=(Pty&&) = delete;
};

// ---------------------------------------------------------------------------
// mock
// ---------------------------------------------------------------------------

TEST_CASE("make_sensor_source: the mock source runs the named profile and exposes itself for commands",
          "[sensor_source_factory]") {
  g_now_ms = 0;
  const Config cfg{.source = Source::Mock, .mock_profile = "Overheat"};
  const SourceResult r = make_sensor_source(cfg, &fake_now);
  REQUIRE(r.source != nullptr);
  CHECK(r.error.empty());
  REQUIRE(r.mock != nullptr);
  CHECK(static_cast<c3::telemetry::SensorSource*>(r.mock) == r.source.get());
  CHECK(r.mock->profile() == MockProfile::Overheat);
  CHECK(r.commands == nullptr);  // the caller builds a MockCommandSink on `mock`
  CHECK(r.source->descriptor() == -1);
}

TEST_CASE("make_sensor_source: the mock runs on the injected clock", "[sensor_source_factory]") {
  g_now_ms = 0;
  const SourceResult r = make_sensor_source(Config{.source = Source::Mock}, &fake_now);
  REQUIRE(r.source != nullptr);
  int frames = 0;
  for (int i = 0; i < 5; ++i) {
    g_now_ms += MockSource::kPeriodMs;
    frames += r.source->poll().has_value() ? 1 : 0;
  }
  CHECK(frames >= 4);  // one per period while the fake clock advances
  const std::uint64_t frozen = g_now_ms;
  int while_frozen = 0;
  for (int i = 0; i < 5; ++i) {
    g_now_ms = frozen;
    while_frozen += r.source->poll().has_value() ? 1 : 0;
  }
  CHECK(while_frozen == 0);  // no wall-clock time leaks in
}

TEST_CASE("make_sensor_source: an unknown mock profile is an error, not a crash", "[sensor_source_factory]") {
  const SourceResult r = make_sensor_source(Config{.source = Source::Mock, .mock_profile = "Bogus"}, &fake_now);
  CHECK(r.source == nullptr);
  CHECK(r.mock == nullptr);
  CHECK(r.commands == nullptr);
  CHECK(r.error == "unknown mock profile 'Bogus'");
}

// ---------------------------------------------------------------------------
// replay
// ---------------------------------------------------------------------------

TEST_CASE("make_sensor_source: replay opens the recording and logs its summary", "[sensor_source_factory]") {
  LogCapture log;
  const ScratchDir dir;
  const std::string path = dir.file("drive.csv", recording());
  g_now_ms = 1'000'000;
  const SourceResult r = make_sensor_source(Config{.source = Source::Replay, .replay_file = path}, &fake_now);
  REQUIRE(r.source != nullptr);
  CHECK(r.error.empty());
  CHECK(r.mock == nullptr);
  CHECK(r.commands == nullptr);  // nothing to command on a recording
  CHECK(r.source->descriptor() == -1);
  CHECK(log.any_contains("3 frames, 0 rows skipped, 2 s of drive"));
  CHECK_FALSE(log.any_contains("[warn]"));

  // frames come out against the injected clock
  int frames = 0;
  for (int i = 0; i <= 25; ++i) {
    g_now_ms = 1'000'000 + static_cast<std::uint64_t>(i) * 100;
    frames += r.source->poll().has_value() ? 1 : 0;
  }
  CHECK(frames == 3);
}

TEST_CASE("make_sensor_source: replay rows that do not parse are warned about and skipped", "[sensor_source_factory]") {
  LogCapture log;
  const ScratchDir dir;
  const std::string path = dir.file("drive.csv", recording("this row is garbage"));
  const SourceResult r = make_sensor_source(Config{.source = Source::Replay, .replay_file = path}, &fake_now);
  REQUIRE(r.source != nullptr);
  CHECK(log.any_contains("drive.csv:5"));  // the warning names the file and line
  CHECK(log.any_contains("3 frames, 1 rows skipped"));
}

TEST_CASE("make_sensor_source: a missing recording is an error naming the path", "[sensor_source_factory]") {
  const ScratchDir dir;
  const std::string path = dir.path_of("absent.csv");
  const SourceResult r = make_sensor_source(Config{.source = Source::Replay, .replay_file = path}, &fake_now);
  CHECK(r.source == nullptr);
  CHECK(r.mock == nullptr);
  CHECK(r.error == "replay: " + path + ": cannot open");
}

TEST_CASE("make_sensor_source: an empty recording is an error", "[sensor_source_factory]") {
  const ScratchDir dir;
  const std::string path = dir.file("empty.csv", "");
  const SourceResult r = make_sensor_source(Config{.source = Source::Replay, .replay_file = path}, &fake_now);
  CHECK(r.source == nullptr);
  CHECK(r.error == "replay: " + path + ": empty file");
}

// ---------------------------------------------------------------------------
// uart
// ---------------------------------------------------------------------------

TEST_CASE("make_sensor_source: the UART source is also the command sink", "[sensor_source_factory]") {
  const Pty pty;
  g_now_ms = 0;
  const Config cfg{.source = Source::Uart, .uart_device = pty.name.data(), .baud = 115200, .stall_timeout_ms = 500};
  const SourceResult r = make_sensor_source(cfg, &fake_now);
  INFO(r.error);
  REQUIRE(r.source != nullptr);
  CHECK(r.error.empty());
  CHECK(r.mock == nullptr);
  REQUIRE(r.commands != nullptr);
  CHECK(dynamic_cast<UartSource*>(r.commands) == dynamic_cast<UartSource*>(r.source.get()));
  CHECK(r.source->descriptor() >= 0);
  CHECK(r.source->health().state == LinkState::NoData);
  // no frame has arrived, so the link is not live and commands are refused as such
  CHECK(r.commands->send_hvac(c3::vehicle::HvacState{}) == c3::vehicle::SubmitResult::Unavailable);
}

TEST_CASE("make_sensor_source: a missing serial device is an error prefixed with the source kind",
          "[sensor_source_factory]") {
  const SourceResult r =
      make_sensor_source(Config{.source = Source::Uart, .uart_device = "/nonexistent-c3console/ttyAMA0"}, &fake_now);
  CHECK(r.source == nullptr);
  CHECK(r.commands == nullptr);
  CHECK_THAT(r.error, StartsWith("uart: /nonexistent-c3console/ttyAMA0: "));
}

TEST_CASE("make_sensor_source: an unsupported baud rate is refused before the device is touched",
          "[sensor_source_factory]") {
  const SourceResult r = make_sensor_source(
      Config{.source = Source::Uart, .uart_device = "/nonexistent-c3console/ttyAMA0", .baud = 1234}, &fake_now);
  CHECK(r.source == nullptr);
  CHECK(r.error == "uart: unsupported baud rate 1234");
}

// ---------------------------------------------------------------------------
// defaults
// ---------------------------------------------------------------------------

TEST_CASE("make_sensor_source: the default Config yields the WarmupDrive mock", "[sensor_source_factory]") {
  g_now_ms = 0;
  const SourceResult r = make_sensor_source(Config{}, &fake_now);
  REQUIRE(r.mock != nullptr);
  CHECK(r.mock->profile() == MockProfile::WarmupDrive);
}

}  // namespace
