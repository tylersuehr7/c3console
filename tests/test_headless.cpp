// headless.cpp — the telemetry path without a display, against the real clock.
#include "app/headless.hpp"
#include "logging/log.hpp"
#include "telemetry/csv_recorder.hpp"
#include "telemetry/frame.hpp"
#include "telemetry/mock_source.hpp"
#include "telemetry/sensor_source.hpp"
#include "utils/clock.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <thread>

using c3::app::run_headless;
using c3::telemetry::CsvRecorder;
using c3::telemetry::Frame;
using c3::telemetry::LinkState;
using c3::telemetry::MockProfile;
using c3::telemetry::MockSource;
using Catch::Matchers::ContainsSubstring;
using namespace std::chrono_literals;

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

class ScratchDir {
public:
  ScratchDir() {
    static int counter = 0;
    path_ = fs::temp_directory_path() / ("c3_headless_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
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

/// Redirects fd 2 into a file so the run's log lines, which the worker writes to stderr, can
/// be checked; also keeps them out of the test output.
class StderrCapture {
public:
  StderrCapture() : previous_level_(c3::logging::get_level()) {
    c3::logging::set_level(c3::logging::Level::Info);
    std::fflush(stderr);
    path_ = (fs::temp_directory_path() / ("c3_headless_stderr_" + std::to_string(::getpid()) + "_" +
                                          std::to_string(reinterpret_cast<std::uintptr_t>(this))))
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
    c3::logging::set_level(previous_level_);
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
  c3::logging::Level previous_level_;
  std::string path_;
  int saved_ = -1;
  int file_ = -1;
};

/// A source that never produces a frame and reports whatever state it is told.
class SilentSource final : public c3::telemetry::SensorSource {
public:
  LinkState state = LinkState::NoData;
  int polls = 0;
  [[nodiscard]] std::optional<Frame> poll() noexcept override {
    ++polls;
    return std::nullopt;
  }
  [[nodiscard]] c3::telemetry::SourceHealth health() const noexcept override { return {.state = state}; }
};

const std::atomic<bool> kNeverStop{false};

TEST_CASE("run_headless: a live source for one second is a success and logs its summaries", "[headless]") {
  const StderrCapture captured;
  MockSource source(MockProfile::WarmupDrive, &c3::utils::steady_now_ms);
  const auto started = Clock::now();
  const int code = run_headless(source, 1, kNeverStop);
  const auto elapsed = Clock::now() - started;
  CHECK(code == 0);
  CHECK(elapsed >= 950ms);
  CHECK(elapsed < 3s);
  CHECK(source.health().frames_ok >= 5);  // 10 Hz, allowing for a slow machine

  const std::string log = captured.text();
  CHECK_THAT(log, ContainsSubstring("(headless) running for 1 s"));
  CHECK_THAT(log, ContainsSubstring("(headless) t="));  // at least one per-second summary
  CHECK_THAT(log, ContainsSubstring("(headless) stopped: frames ok="));
  CHECK_THAT(log, !ContainsSubstring("I/O queue overflow"));
}

TEST_CASE("run_headless: a source that never delivers a frame fails after the time is up", "[headless]") {
  const StderrCapture captured;
  SilentSource source;
  const auto started = Clock::now();
  const int code = run_headless(source, 1, kNeverStop);
  CHECK(code == 1);
  CHECK(Clock::now() - started >= 950ms);
  CHECK(source.polls >= 5);  // polled on the tick even without a descriptor
  CHECK_THAT(captured.text(), ContainsSubstring("no frame yet; link=no-data"));
}

TEST_CASE("run_headless: with no time limit it runs until stopped", "[headless]") {
  const StderrCapture captured;
  SilentSource source;
  std::atomic<bool> stop{false};
  std::thread stopper([&] {
    std::this_thread::sleep_for(300ms);
    stop.store(true);
  });
  const auto started = Clock::now();
  const int code = run_headless(source, 0, stop);
  stopper.join();
  const auto elapsed = Clock::now() - started;
  CHECK(code == 1);
  CHECK(elapsed >= 250ms);
  CHECK(elapsed < 2s);
  CHECK_THAT(captured.text(), ContainsSubstring("running until interrupted"));
}

TEST_CASE("run_headless: a stop already raised returns at once", "[headless]") {
  const StderrCapture captured;
  MockSource source(MockProfile::WarmupDrive, &c3::utils::steady_now_ms);
  const std::atomic<bool> stop{true};
  const auto started = Clock::now();
  const int code = run_headless(source, 30, stop);
  CHECK(code == 1);  // no tick ran, so no frame was ever seen
  CHECK(Clock::now() - started < 500ms);
}

TEST_CASE("run_headless: every accepted frame is recorded and the totals are logged", "[headless]") {
  const StderrCapture captured;
  const ScratchDir dir;
  auto opened = CsvRecorder::open(dir.path_of("drive.csv"));
  REQUIRE(opened.recorder != nullptr);
  MockSource source(MockProfile::HighwayCruise, &c3::utils::steady_now_ms);
  const int code = run_headless(source, 1, kNeverStop, opened.recorder.get());
  CHECK(code == 0);
  CHECK(opened.recorder->frames_written() == source.health().frames_ok);
  CHECK(opened.recorder->frames_written() >= 5);
  CHECK(opened.recorder->write_errors() == 0);
  CHECK_THAT(captured.text(), ContainsSubstring("(headless) recorded "));
  CHECK_THAT(captured.text(), ContainsSubstring(dir.path_of("drive.csv")));
}

TEST_CASE("run_headless: the LinkLoss profile is reported as a stall, not hidden", "[headless]") {
  // LinkLoss goes quiet after 10 s; that is too long for a unit test, so this only checks the
  // live phase runs clean. The stall itself is covered by the ingestor and mock source tests.
  const StderrCapture captured;
  MockSource source(MockProfile::LinkLoss, &c3::utils::steady_now_ms);
  CHECK(run_headless(source, 1, kNeverStop) == 0);
  CHECK_THAT(captured.text(), !ContainsSubstring("telemetry stalled"));
}

}  // namespace
