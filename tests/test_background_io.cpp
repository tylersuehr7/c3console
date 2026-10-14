// background_io.cpp — the worker that records, logs, saves settings and reads the clock.
#include "app/background_io.hpp"
#include "app/settings_store.hpp"
#include "logging/log.hpp"
#include "render/settings.hpp"
#include "telemetry/csv_recorder.hpp"
#include "telemetry/frame.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>

using c3::app::BackgroundIO;
using c3::render::Settings;
using c3::render::ThemePreference;
using c3::telemetry::CsvRecorder;
using c3::telemetry::Frame;
using Catch::Matchers::ContainsSubstring;

namespace {

namespace fs = std::filesystem;

class ScratchDir {
public:
  ScratchDir() {
    static int counter = 0;
    path_ = fs::temp_directory_path() / ("c3_bgio_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
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

  [[nodiscard]] std::string path_of(std::string_view name) const { return (path_ / name).string(); }

private:
  fs::path path_;
};

/// Redirects fd 2 into a file for the object's lifetime, so what the worker writes with
/// fwrite(stderr) can be read back.
class StderrCapture {
public:
  StderrCapture() {
    std::fflush(stderr);
    path_ = (fs::temp_directory_path() /
             ("c3_stderr_" + std::to_string(::getpid()) + "_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
                .string();
    saved_ = ::dup(2);
    file_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    REQUIRE(saved_ >= 0);
    REQUIRE(file_ >= 0);
    REQUIRE(::dup2(file_, 2) == 2);
  }
  ~StderrCapture() {
    std::fflush(stderr);
    static_cast<void>(::dup2(saved_, 2));
    static_cast<void>(::close(saved_));
    static_cast<void>(::close(file_));
    static_cast<void>(::unlink(path_.c_str()));
  }
  StderrCapture(const StderrCapture&) = delete;
  StderrCapture& operator=(const StderrCapture&) = delete;
  StderrCapture(StderrCapture&&) = delete;
  StderrCapture& operator=(StderrCapture&&) = delete;

  [[nodiscard]] std::string text() const {
    std::fflush(stderr);
    std::ifstream in(path_, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  }

private:
  std::string path_;
  int saved_ = -1;
  int file_ = -1;
};

[[nodiscard]] std::size_t count_lines(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return static_cast<std::size_t>(
      std::count(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>(), '\n'));
}

[[nodiscard]] Frame frame_at(std::uint32_t t_ms) noexcept {
  return Frame{.t_ms = t_ms, .fuel_fraction = 0.5F, .oil_psi = 40.0F, .coolant_f = 190.0F, .voltage_v = 14.0F};
}

constexpr Settings kCustom{.theme = ThemePreference::Light, .accent = 0x2DD4BF};

// ---------------------------------------------------------------------------
// recording
// ---------------------------------------------------------------------------

TEST_CASE("BackgroundIO: without a recorder, record() is free and nothing is dropped", "[background_io]") {
  BackgroundIO io(nullptr);
  for (std::uint32_t t = 0; t < 2000; ++t) {
    io.record(frame_at(t));
  }
  io.finish();
  CHECK(io.dropped_frames() == 0);
  CHECK(io.dropped_logs() == 0);
  CHECK(io.settings_errors() == 0);
}

TEST_CASE("BackgroundIO: queued frames reach the recorder before finish() returns", "[background_io]") {
  const ScratchDir dir;
  auto opened = CsvRecorder::open(dir.path_of("drive.csv"));
  REQUIRE(opened.recorder != nullptr);
  {
    BackgroundIO io(opened.recorder.get());
    for (std::uint32_t t = 0; t < 10; ++t) {
      io.record(frame_at(t * 100));
    }
    io.finish();
    CHECK(opened.recorder->frames_written() == 10);
    CHECK(io.dropped_frames() == 0);
  }
  opened.recorder.reset();                             // flush and close
  CHECK(count_lines(dir.path_of("drive.csv")) == 11);  // header plus ten rows
}

TEST_CASE("BackgroundIO: a burst beyond the queue depth is dropped and counted, never blocked on", "[background_io]") {
  const ScratchDir dir;
  auto opened = CsvRecorder::open(dir.path_of("drive.csv"));
  REQUIRE(opened.recorder != nullptr);
  constexpr std::uint32_t kBurst = 3 * BackgroundIO::kFrameQueueDepth;
  BackgroundIO io(opened.recorder.get());
  for (std::uint32_t t = 0; t < kBurst; ++t) {
    io.record(frame_at(t));
  }
  io.finish();
  // How many the worker drained mid-burst depends on scheduling; the accounting does not.
  CHECK(opened.recorder->frames_written() + io.dropped_frames() == kBurst);
  CHECK(opened.recorder->frames_written() >= BackgroundIO::kFrameQueueDepth);
}

TEST_CASE("BackgroundIO: finish() is idempotent and the destructor finishes too", "[background_io]") {
  const ScratchDir dir;
  auto opened = CsvRecorder::open(dir.path_of("drive.csv"));
  REQUIRE(opened.recorder != nullptr);
  {
    BackgroundIO io(opened.recorder.get());
    io.record(frame_at(1));
    io.finish();
    io.finish();
    CHECK(opened.recorder->frames_written() == 1);
  }
  {
    BackgroundIO io(opened.recorder.get());
    io.record(frame_at(2));
  }  // no explicit finish
  CHECK(opened.recorder->frames_written() == 2);
}

// ---------------------------------------------------------------------------
// logging
// ---------------------------------------------------------------------------

TEST_CASE("BackgroundIO: lines handed to the sink come out on stderr in order", "[background_io]") {
  const StderrCapture captured;
  {
    BackgroundIO io(nullptr);
    const c3::logging::Sink sink = io.log_sink();
    sink.emit(sink.context, "first line\n");
    sink.emit(sink.context, "second line\n");
    sink.emit(sink.context, "third line\n");
    io.finish();
    CHECK(io.dropped_logs() == 0);
  }
  CHECK(captured.text() == "first line\nsecond line\nthird line\n");
}

TEST_CASE("BackgroundIO: the sink works through the project logger", "[background_io]") {
  const StderrCapture captured;
  {
    BackgroundIO io(nullptr);
    const c3::logging::ScopedSink installed(io.log_sink());
    c3::logging::error("test", "routed through the worker");
    io.finish();
  }
  CHECK_THAT(captured.text(), ContainsSubstring("[error] (test) routed through the worker"));
}

TEST_CASE("BackgroundIO: a line longer than the slot is truncated, not split or dropped", "[background_io]") {
  const StderrCapture captured;
  const std::string huge(BackgroundIO::kMaxLogLine + 500, 'x');
  {
    BackgroundIO io(nullptr);
    const c3::logging::Sink sink = io.log_sink();
    sink.emit(sink.context, huge);
    io.finish();
    CHECK(io.dropped_logs() == 0);
  }
  CHECK(captured.text().size() == BackgroundIO::kMaxLogLine);
}

TEST_CASE("BackgroundIO: a log burst beyond the queue depth is dropped and counted", "[background_io]") {
  const StderrCapture captured;
  constexpr std::string_view kLine = "0123456789\n";
  constexpr unsigned kBurst = 3 * BackgroundIO::kLogQueueDepth;
  unsigned dropped = 0;
  {
    BackgroundIO io(nullptr);
    const c3::logging::Sink sink = io.log_sink();
    for (unsigned i = 0; i < kBurst; ++i) {
      sink.emit(sink.context, kLine);
    }
    io.finish();
    dropped = io.dropped_logs();
  }
  CHECK(captured.text().size() == (kBurst - dropped) * kLine.size());
  CHECK(kBurst - dropped >= BackgroundIO::kLogQueueDepth);
}

// ---------------------------------------------------------------------------
// settings
// ---------------------------------------------------------------------------

TEST_CASE("BackgroundIO: save() writes the settings file before finish() returns", "[background_io]") {
  const ScratchDir dir;
  const std::string path = dir.path_of("settings.toml");
  {
    BackgroundIO io(nullptr, path);
    io.save(kCustom);
    io.finish();
    CHECK(io.settings_errors() == 0);
  }
  const auto loaded = c3::app::load_settings(path, Settings{});
  CHECK(loaded.status == c3::app::SettingsStatus::Loaded);
  CHECK(loaded.settings == kCustom);
}

TEST_CASE("BackgroundIO: a burst of saves ends with the last value on disk", "[background_io]") {
  const ScratchDir dir;
  const std::string path = dir.path_of("settings.toml");
  {
    BackgroundIO io(nullptr, path);
    for (std::uint32_t accent = 0; accent < 50; ++accent) {
      io.save(Settings{.accent = accent});
    }
    io.save(kCustom);
    io.finish();
  }
  CHECK(c3::app::load_settings(path, Settings{}).settings == kCustom);
}

TEST_CASE("BackgroundIO: without a settings path, save() is discarded without error", "[background_io]") {
  BackgroundIO io(nullptr);
  io.save(kCustom);
  io.finish();
  CHECK(io.settings_errors() == 0);
}

TEST_CASE("BackgroundIO: a settings write that fails is counted and reported from the worker itself",
          "[background_io]") {
  const ScratchDir dir;
  const std::string path = dir.path_of("missing-dir/settings.toml");
  const StderrCapture captured;
  {
    BackgroundIO io(nullptr, path);
    const c3::logging::ScopedSink installed(io.log_sink());  // the worker's own line must not re-enter the queue
    io.save(kCustom);
    io.finish();
    CHECK(io.settings_errors() == 1);
    CHECK(io.dropped_logs() == 0);
  }
  CHECK_THAT(captured.text(), ContainsSubstring("(settings) cannot create " + path + ".tmp"));
  CHECK_FALSE(fs::exists(path));
}

// ---------------------------------------------------------------------------
// wall clock
// ---------------------------------------------------------------------------

TEST_CASE("BackgroundIO: wall_clock reads the local time of day in 12-hour form", "[background_io]") {
  const std::time_t before = std::time(nullptr);
  BackgroundIO io(nullptr);
  const c3::render::WallClock clock = io.wall_clock();
  const std::time_t after = std::time(nullptr);
  io.finish();

  CHECK(clock.hour12 >= 1);
  CHECK(clock.hour12 <= 12);
  CHECK(clock.minute >= 0);
  CHECK(clock.minute <= 59);

  // the same reading, derived independently, allowing the minute to have rolled over
  const auto expected = [](std::time_t t) {
    std::tm local{};
    REQUIRE(localtime_r(&t, &local) != nullptr);
    return c3::render::WallClock{
        .hour12 = local.tm_hour % 12 == 0 ? 12 : local.tm_hour % 12, .minute = local.tm_min, .pm = local.tm_hour >= 12};
  };
  const auto a = expected(before);
  const auto b = expected(after);
  const bool matches_a = clock.hour12 == a.hour12 && clock.minute == a.minute && clock.pm == a.pm;
  const bool matches_b = clock.hour12 == b.hour12 && clock.minute == b.minute && clock.pm == b.pm;
  CHECK((matches_a || matches_b));
}

TEST_CASE("BackgroundIO: cannot be copied or moved", "[background_io]") {
  STATIC_CHECK_FALSE(std::is_copy_constructible_v<BackgroundIO>);
  STATIC_CHECK_FALSE(std::is_move_constructible_v<BackgroundIO>);
}

}  // namespace
