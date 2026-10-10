#include "telemetry/csv_codec.hpp"
#include "telemetry/csv_recorder.hpp"
#include "telemetry/frame.hpp"
#include "telemetry/replay_source.hpp"
#include "telemetry/sensor_source.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

using c3::telemetry::Channel;
using c3::telemetry::CsvRecorder;
using c3::telemetry::Fault;
using c3::telemetry::FaultKind;
using c3::telemetry::Frame;
using c3::telemetry::kCsvHeader;
using c3::telemetry::LinkState;
using c3::telemetry::ReplaySource;
using c3::telemetry::SensorSource;
using c3::telemetry::SourceHealth;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinAbs;

CATCH_REGISTER_ENUM(LinkState, LinkState::NoData, LinkState::Live, LinkState::Stalled, LinkState::Failed)

namespace {

// ---------------------------------------------------------------------------
// Fake clock: MonotonicClock is a plain function pointer, so the time source is file-scope
// state that each case sets before polling.
// ---------------------------------------------------------------------------

std::uint64_t g_now_ms = 0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
[[nodiscard]] std::uint64_t fake_now() noexcept {
  return g_now_ms;
}

constexpr std::uint32_t kTimeout = 1000;
constexpr std::string_view kBody = ",12843,8211,19004,21877,0.6200,41.20,196.40,14.100,";

/// One well-formed row at MCU time `t`, optionally with a fault list.
[[nodiscard]] std::string row(std::uint32_t t, std::string_view faults = {}) {
  return std::to_string(t) + std::string(kBody) + std::string(faults);
}

/// Header plus the given lines, each newline-terminated.
[[nodiscard]] std::string csv(std::initializer_list<std::string> lines) {
  std::string out(kCsvHeader);
  out += "\n";
  for (const auto& line : lines) {
    out += line;
    out += "\n";
  }
  return out;
}

[[nodiscard]] std::unique_ptr<ReplaySource> open_text(std::string_view text, std::uint32_t timeout = kTimeout,
                                                      std::uint64_t start_ms = 1'000'000) {
  g_now_ms = start_ms;
  auto result = ReplaySource::from_text(text, &fake_now, timeout);
  INFO("error: " << result.error);
  REQUIRE(result.source != nullptr);
  REQUIRE(result.error.empty());
  return std::move(result.source);
}

[[nodiscard]] std::optional<Frame> poll_at(ReplaySource& source, std::uint64_t t_ms) {
  g_now_ms = t_ms;
  return source.poll();
}

[[nodiscard]] Frame frame_at(ReplaySource& source, std::uint64_t t_ms) {
  const auto f = poll_at(source, t_ms);
  INFO("poll at " << t_ms << " ms returned no frame");
  REQUIRE(f.has_value());
  return *f;
}

void expect_nothing_at(ReplaySource& source, std::uint64_t t_ms, LinkState expected_state) {
  INFO("poll at " << t_ms << " ms");
  CHECK_FALSE(poll_at(source, t_ms).has_value());
  CHECK(source.health().state == expected_state);
}

/// A fresh scratch directory, removed when the test case ends.
class ScratchDir {
public:
  ScratchDir() {
    static unsigned counter = 0;
    path_ = std::filesystem::temp_directory_path() /
            ("c3console_replay_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
    std::filesystem::remove_all(path_);
    REQUIRE(std::filesystem::create_directory(path_));
  }
  ~ScratchDir() { std::filesystem::remove_all(path_); }
  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;
  ScratchDir(ScratchDir&&) = delete;
  ScratchDir& operator=(ScratchDir&&) = delete;

  [[nodiscard]] std::string write(std::string_view name, std::string_view content) const {
    const auto path = (path_ / name).string();
    std::ofstream out(path, std::ios::binary);
    REQUIRE(out.is_open());
    out << content;
    return path;
  }
  [[nodiscard]] std::string file(std::string_view name) const { return (path_ / name).string(); }

private:
  std::filesystem::path path_;
};

// ---------------------------------------------------------------------------
// Type properties
// ---------------------------------------------------------------------------

TEST_CASE("ReplaySource: is a non-copyable SensorSource with no descriptor", "[replay_source]") {
  STATIC_CHECK(std::is_base_of_v<SensorSource, ReplaySource>);
  STATIC_CHECK_FALSE(std::is_copy_constructible_v<ReplaySource>);
  STATIC_CHECK_FALSE(std::is_move_constructible_v<ReplaySource>);
  STATIC_CHECK(ReplaySource::kMaxGapMs == 10'000);
  auto source = open_text(csv({row(0)}));
  STATIC_CHECK(noexcept(source->poll()));
  STATIC_CHECK(noexcept(source->health()));
  CHECK(source->descriptor() == -1);
}

// ---------------------------------------------------------------------------
// from_text: accepted input
// ---------------------------------------------------------------------------

TEST_CASE("from_text: a clean recording loads with no warnings and a summary", "[replay_source]") {
  g_now_ms = 0;
  auto result = ReplaySource::from_text(csv({row(0), row(1000), row(2000)}), &fake_now, kTimeout);
  REQUIRE(result.source != nullptr);
  CHECK(result.error.empty());
  CHECK(result.warnings.empty());
  CHECK(result.summary == "<text>: 3 frames, 0 rows skipped, 2 s of drive");
  CHECK(result.source->frame_count() == 3);
  CHECK(result.source->rows_skipped() == 0);
  CHECK_FALSE(result.source->finished());
}

TEST_CASE("from_text: the name parameter labels the summary and warnings", "[replay_source]") {
  g_now_ms = 0;
  auto result = ReplaySource::from_text(csv({row(0), "garbage", row(100)}), &fake_now, kTimeout, "drive.csv");
  REQUIRE(result.source != nullptr);
  CHECK_THAT(result.summary, StartsWith("drive.csv: 2 frames, 1 rows skipped"));
  REQUIRE(result.warnings.size() == 1);
  CHECK_THAT(result.warnings[0], StartsWith("drive.csv:3: "));
}

TEST_CASE("from_text: frames carry exactly what the rows said and nothing the CSV does not record", "[replay_source]") {
  auto source = open_text(csv({row(184523, "OIL_LOW|SENDER_OPEN_CLT")}));
  const Frame f = frame_at(*source, g_now_ms);
  CHECK(f.t_ms == 184523U);
  CHECK(f.raw[static_cast<std::size_t>(Channel::Fuel)] == 12843);
  CHECK(f.raw[static_cast<std::size_t>(Channel::Oil)] == 8211);
  CHECK(f.raw[static_cast<std::size_t>(Channel::Coolant)] == 19004);
  CHECK(f.raw[static_cast<std::size_t>(Channel::Volt)] == 21877);
  CHECK_THAT(f.fuel_fraction, WithinAbs(0.62, 1e-6));
  CHECK_THAT(f.oil_psi, WithinAbs(41.2, 1e-5));
  CHECK_THAT(f.coolant_f, WithinAbs(196.4, 1e-4));
  CHECK_THAT(f.voltage_v, WithinAbs(14.1, 1e-5));
  REQUIRE(f.faults.size() == 2);
  CHECK(f.faults[0].kind == FaultKind::OilLow);
  CHECK(f.faults[1].kind == FaultKind::SenderOpen);
  CHECK(f.faults[1].channel == Channel::Coolant);
  CHECK_FALSE(f.relays.has_value());
  CHECK_FALSE(f.reverse);
}

TEST_CASE("from_text: CRLF line endings and a CR-terminated header are accepted", "[replay_source]") {
  const std::string text = std::string(kCsvHeader) + "\r\n" + row(0) + "\r\n" + row(100) + "\r\n";
  auto source = open_text(text);
  CHECK(source->frame_count() == 2);
  CHECK(source->rows_skipped() == 0);
  CHECK(frame_at(*source, g_now_ms).t_ms == 0);
}

TEST_CASE("from_text: a final row without a trailing newline is still read", "[replay_source]") {
  const std::string text = std::string(kCsvHeader) + "\n" + row(0) + "\n" + row(100);
  auto source = open_text(text);
  CHECK(source->frame_count() == 2);
}

TEST_CASE("from_text: blank lines anywhere are ignored", "[replay_source]") {
  const std::string text = "\n\r\n" + std::string(kCsvHeader) + "\n\n" + row(0) + "\n\r\n" + row(100) + "\n\n\n";
  auto source = open_text(text);
  CHECK(source->frame_count() == 2);
  CHECK(source->rows_skipped() == 0);
}

TEST_CASE("from_text: a long recording loads in full", "[replay_source]") {
  std::string text(kCsvHeader);
  text += "\n";
  for (std::uint32_t i = 0; i < 10'000; ++i) {
    text += row(i * 100);
    text += "\n";
  }
  g_now_ms = 0;
  auto result = ReplaySource::from_text(text, &fake_now, kTimeout);
  REQUIRE(result.source != nullptr);
  CHECK(result.source->frame_count() == 10'000);
  CHECK(result.summary == "<text>: 10000 frames, 0 rows skipped, 999 s of drive");
}

// ---------------------------------------------------------------------------
// from_text: rejected input
// ---------------------------------------------------------------------------

TEST_CASE("from_text: an empty recording is an error", "[replay_source]") {
  for (const std::string_view text : {"", "\n", "\r\n\r\n", "\n\n\n"}) {
    g_now_ms = 0;
    auto result = ReplaySource::from_text(text, &fake_now, kTimeout);
    CHECK(result.source == nullptr);
    CHECK(result.error == "<text>: empty file");
    CHECK(result.warnings.empty());
  }
}

TEST_CASE("from_text: a header with no rows is an error", "[replay_source]") {
  for (const auto& text :
       {std::string(kCsvHeader), std::string(kCsvHeader) + "\n", std::string(kCsvHeader) + "\n\n\r\n"}) {
    g_now_ms = 0;
    auto result = ReplaySource::from_text(text, &fake_now, kTimeout);
    CHECK(result.source == nullptr);
    CHECK(result.error == "<text>: no usable rows");
  }
}

TEST_CASE("from_text: a file whose first line is not the header is an error", "[replay_source]") {
  g_now_ms = 0;
  auto result = ReplaySource::from_text(row(0) + "\n" + row(100) + "\n", &fake_now, kTimeout, "drive.csv");
  CHECK(result.source == nullptr);
  CHECK(result.error == "drive.csv:1: expected the header '" + std::string(kCsvHeader) + "'");

  auto wrong = ReplaySource::from_text("timestamp_ms,fuel,oil\n" + row(0) + "\n", &fake_now, kTimeout);
  CHECK(wrong.source == nullptr);
  CHECK_THAT(wrong.error, ContainsSubstring("expected the header"));

  auto cased = ReplaySource::from_text(
      "TIMESTAMP_MS,fuel_raw,oil_raw,clt_raw,volt_raw,fuel,oil,clt,volt,faults\n" + row(0) + "\n", &fake_now, kTimeout);
  CHECK(cased.source == nullptr);
}

TEST_CASE("from_text: a recording where every row is malformed is an error, with the reasons kept", "[replay_source]") {
  g_now_ms = 0;
  auto result = ReplaySource::from_text(csv({"bad", "1,2,3", "x" + std::string(kBody)}), &fake_now, kTimeout);
  CHECK(result.source == nullptr);
  CHECK(result.error == "<text>: no usable rows");
  REQUIRE(result.warnings.size() == 3);
  CHECK_THAT(result.warnings[0], StartsWith("<text>:2: "));
  CHECK_THAT(result.warnings[1], StartsWith("<text>:3: "));
  CHECK_THAT(result.warnings[2], StartsWith("<text>:4: "));
}

// ---------------------------------------------------------------------------
// from_text: malformed rows are counted, reported and skipped
// ---------------------------------------------------------------------------

TEST_CASE("from_text: each kind of bad row is skipped with its reason and line number", "[replay_source]") {
  const std::string nine_faults =
      row(600,
          "OIL_LOW|CLT_HIGH|FUEL_LOW|VOLT_LOW|VOLT_HIGH|SENDER_OPEN_FUEL|SENDER_OPEN_OIL|SENDER_SHORT_VOLT|"
          "SENDER_SHORT_OIL");
  g_now_ms = 0;
  auto result = ReplaySource::from_text(csv({
                                            row(0),
                                            "100,12843,8211,19004,21877,0.6200,41.20,196.40",  // 9 fields
                                            row(200),
                                            "300,12843,8211,19004,21877,abc,41.20,196.40,14.100,",     // bad number
                                            "400,12843,8211,19004,21877,0.6200,41.20,999.00,14.100,",  // out of range
                                            row(500, "BRAKE_LOW"),                                     // unknown fault
                                            nine_faults,                                               // too many
                                            row(700),
                                        }),
                                        &fake_now, kTimeout);
  REQUIRE(result.source != nullptr);
  CHECK(result.source->frame_count() == 3);
  CHECK(result.source->rows_skipped() == 5);
  CHECK(result.source->health().frames_malformed == 5);
  REQUIRE(result.warnings.size() == 5);
  CHECK(result.warnings[0] == "<text>:3: wrong field count; row skipped");
  CHECK(result.warnings[1] == "<text>:5: bad number; row skipped");
  CHECK(result.warnings[2] == "<text>:6: value out of range; row skipped");
  CHECK(result.warnings[3] == "<text>:7: unknown fault code; row skipped");
  CHECK(result.warnings[4] == "<text>:8: too many faults; row skipped");
  CHECK(result.summary == "<text>: 3 frames, 5 rows skipped, 0 s of drive");
}

TEST_CASE("from_text: warning line numbers count blank lines so they match an editor", "[replay_source]") {
  const std::string text = std::string(kCsvHeader) + "\n" + "\n" + row(0) + "\n" + "\n" + "\n" + "broken\n";
  g_now_ms = 0;
  auto result = ReplaySource::from_text(text, &fake_now, kTimeout);
  REQUIRE(result.source != nullptr);
  REQUIRE(result.warnings.size() == 1);
  CHECK_THAT(result.warnings[0], StartsWith("<text>:6: "));
}

TEST_CASE("from_text: a repeated header inside the file is skipped as a malformed row", "[replay_source]") {
  // Two recordings concatenated with cat: the second header is not data.
  g_now_ms = 0;
  auto result =
      ReplaySource::from_text(csv({row(0), row(100), std::string(kCsvHeader), row(200)}), &fake_now, kTimeout);
  REQUIRE(result.source != nullptr);
  CHECK(result.source->frame_count() == 3);
  CHECK(result.source->rows_skipped() == 1);
  REQUIRE(result.warnings.size() == 1);
  CHECK_THAT(result.warnings[0], ContainsSubstring("bad number"));
}

TEST_CASE("from_text: skipped rows do not disturb the cadence of the rows around them", "[replay_source]") {
  auto source = open_text(csv({row(0), "junk", row(200)}), kTimeout, 0);
  CHECK(frame_at(*source, 0).t_ms == 0);
  CHECK_FALSE(poll_at(*source, 100).has_value());
  CHECK_FALSE(poll_at(*source, 199).has_value());
  CHECK(frame_at(*source, 200).t_ms == 200);
}

// ---------------------------------------------------------------------------
// open(path)
// ---------------------------------------------------------------------------

TEST_CASE("open: a missing file is an error naming the path", "[replay_source]") {
  const ScratchDir dir;
  const std::string path = dir.file("absent.csv");
  g_now_ms = 0;
  auto result = ReplaySource::open(path, &fake_now, kTimeout);
  CHECK(result.source == nullptr);
  CHECK(result.error == "replay: cannot open " + path);
  CHECK(result.warnings.empty());
  CHECK(result.summary.empty());
}

TEST_CASE("open: reads a file and labels its summary and warnings with the path", "[replay_source]") {
  const ScratchDir dir;
  const std::string path = dir.write("drive.csv", csv({row(0), "junk", row(100)}));
  g_now_ms = 0;
  auto result = ReplaySource::open(path, &fake_now, kTimeout);
  INFO("error: " << result.error);
  REQUIRE(result.source != nullptr);
  CHECK(result.source->frame_count() == 2);
  CHECK_THAT(result.summary, StartsWith(path + ": 2 frames, 1 rows skipped"));
  REQUIRE(result.warnings.size() == 1);
  CHECK_THAT(result.warnings[0], StartsWith(path + ":3: "));
}

TEST_CASE("open: an empty file and a header-only file are errors", "[replay_source]") {
  const ScratchDir dir;
  g_now_ms = 0;
  auto empty = ReplaySource::open(dir.write("empty.csv", ""), &fake_now, kTimeout);
  CHECK(empty.source == nullptr);
  CHECK_THAT(empty.error, EndsWith(": empty file"));

  auto header_only = ReplaySource::open(dir.write("header.csv", std::string(kCsvHeader) + "\n"), &fake_now, kTimeout);
  CHECK(header_only.source == nullptr);
  CHECK_THAT(header_only.error, EndsWith(": no usable rows"));
}

TEST_CASE("open: a drive recorded by CsvRecorder replays frame for frame", "[replay_source]") {
  const ScratchDir dir;
  const std::string path = dir.file("drive.csv");

  std::vector<Frame> recorded;
  {
    auto opened = CsvRecorder::open(path);
    REQUIRE(opened.recorder != nullptr);
    for (std::uint32_t i = 0; i < 30; ++i) {
      Frame f;
      f.t_ms = 5000 + i * 100;
      f.raw = {static_cast<std::int32_t>(i), -static_cast<std::int32_t>(i), 100, 200};
      f.fuel_fraction = 0.5F - static_cast<float>(i) * 0.001F;
      f.oil_psi = 40.0F + static_cast<float>(i % 5);
      f.coolant_f = 180.0F + static_cast<float>(i);
      f.voltage_v = 14.1F;
      if (i % 7 == 0) {
        REQUIRE(f.faults.push_back(Fault{.kind = FaultKind::OilLow}));
      }
      opened.recorder->record(f);
      recorded.push_back(f);
    }
  }

  g_now_ms = 0;
  auto result = ReplaySource::open(path, &fake_now, kTimeout);
  INFO("error: " << result.error);
  REQUIRE(result.source != nullptr);
  REQUIRE(result.source->frame_count() == recorded.size());
  CHECK(result.warnings.empty());

  for (std::size_t i = 0; i < recorded.size(); ++i) {
    const Frame f = frame_at(*result.source, i * 100);
    CHECK(f.t_ms == recorded[i].t_ms);
    CHECK(f.raw == recorded[i].raw);
    CHECK_THAT(f.fuel_fraction, WithinAbs(recorded[i].fuel_fraction, 0.00005 + 1e-6));
    CHECK_THAT(f.oil_psi, WithinAbs(recorded[i].oil_psi, 0.005 + 1e-4));
    CHECK_THAT(f.coolant_f, WithinAbs(recorded[i].coolant_f, 0.005 + 1e-4));
    CHECK(f.faults.size() == recorded[i].faults.size());
  }
  CHECK(result.source->finished());
}

// ---------------------------------------------------------------------------
// poll: cadence
// ---------------------------------------------------------------------------

TEST_CASE("poll: before the first poll the link reports no data but already knows the skipped rows",
          "[replay_source]") {
  auto source = open_text(csv({row(0), "junk", row(100)}));
  const SourceHealth h = source->health();
  CHECK(h.state == LinkState::NoData);
  CHECK(h.frames_ok == 0);
  CHECK(h.frames_bad_crc == 0);
  CHECK(h.frames_malformed == 1);
  CHECK(h.last_frame_monotonic_ms == 0);
}

TEST_CASE("poll: the first frame is released on the first poll at any clock value", "[replay_source]") {
  for (const std::uint64_t start : {std::uint64_t{0}, std::uint64_t{37}, std::uint64_t{1'000'000}}) {
    auto source = open_text(csv({row(184523), row(184623)}), kTimeout, start);
    const Frame f = frame_at(*source, start);
    CHECK(f.t_ms == 184523U);
    const SourceHealth h = source->health();
    CHECK(h.state == LinkState::Live);
    CHECK(h.frames_ok == 1);
    CHECK(h.last_frame_monotonic_ms == start);
  }
}

TEST_CASE("poll: rows are released at their recorded spacing, not before", "[replay_source]") {
  auto source = open_text(csv({row(0), row(100), row(250)}), kTimeout, 500);
  CHECK(frame_at(*source, 500).t_ms == 0);
  CHECK_FALSE(poll_at(*source, 599).has_value());
  CHECK(frame_at(*source, 600).t_ms == 100);
  CHECK_FALSE(poll_at(*source, 700).has_value());
  CHECK_FALSE(poll_at(*source, 749).has_value());
  CHECK(frame_at(*source, 750).t_ms == 250);
  CHECK(source->health().frames_ok == 3);
}

TEST_CASE("poll: cadence comes from MCU timestamps, independent of their absolute value", "[replay_source]") {
  auto source = open_text(csv({row(184523), row(184623), row(184723)}), kTimeout, 0);
  CHECK(frame_at(*source, 0).t_ms == 184523U);
  CHECK_FALSE(poll_at(*source, 99).has_value());
  CHECK(frame_at(*source, 100).t_ms == 184623U);
  CHECK(frame_at(*source, 200).t_ms == 184723U);
}

TEST_CASE("poll: a frame that is not due yet leaves health untouched within the stall timeout", "[replay_source]") {
  auto source = open_text(csv({row(0), row(5000)}), kTimeout, 0);
  static_cast<void>(frame_at(*source, 0));
  const SourceHealth before = source->health();
  CHECK_FALSE(poll_at(*source, 500).has_value());
  const SourceHealth after = source->health();
  CHECK(after.state == LinkState::Live);
  CHECK(after.frames_ok == before.frames_ok);
  CHECK(after.last_frame_monotonic_ms == before.last_frame_monotonic_ms);
}

TEST_CASE("poll: releases one frame per call even when several are overdue", "[replay_source]") {
  // Unlike the mock, a late poller is not skipped ahead: every recorded frame is delivered.
  auto source = open_text(csv({row(0), row(100), row(200), row(300)}), kTimeout, 0);
  CHECK(frame_at(*source, 0).t_ms == 0);
  CHECK(frame_at(*source, 1000).t_ms == 100);
  CHECK(frame_at(*source, 1000).t_ms == 200);
  CHECK(frame_at(*source, 1000).t_ms == 300);
  CHECK(source->finished());
  CHECK(source->health().frames_ok == 4);
}

TEST_CASE("poll: every frame is delivered exactly once in order", "[replay_source]") {
  std::string text(kCsvHeader);
  text += "\n";
  for (std::uint32_t i = 0; i < 500; ++i) {
    text += row(i * 100);
    text += "\n";
  }
  auto source = open_text(text, kTimeout, 0);
  std::uint32_t expected = 0;
  for (std::uint64_t now = 0; now < 50'000; now += 50) {  // poll at twice the recorded rate
    if (const auto f = poll_at(*source, now)) {
      CHECK(f->t_ms == expected);
      expected += 100;
    }
  }
  CHECK(expected == 50'000);
  CHECK(source->health().frames_ok == 500);
  CHECK(source->finished());
}

// ---------------------------------------------------------------------------
// poll: gaps, stalls and the gap cap
// ---------------------------------------------------------------------------

TEST_CASE("poll: a gap in the recording replays as a stall after the timeout", "[replay_source]") {
  auto source = open_text(csv({row(0), row(5000)}), kTimeout, 0);
  CHECK(frame_at(*source, 0).t_ms == 0);
  expect_nothing_at(*source, 999, LinkState::Live);
  expect_nothing_at(*source, 1000, LinkState::Live);  // age equal to the timeout is not yet stale
  expect_nothing_at(*source, 1001, LinkState::Stalled);
  expect_nothing_at(*source, 4999, LinkState::Stalled);
  CHECK(frame_at(*source, 5000).t_ms == 5000);
  CHECK(source->health().state == LinkState::Live);
  CHECK(source->health().last_frame_monotonic_ms == 5000);
}

TEST_CASE("poll: the stall timeout parameter is honoured", "[replay_source]") {
  auto source = open_text(csv({row(0), row(5000)}), 200, 0);
  static_cast<void>(frame_at(*source, 0));
  expect_nothing_at(*source, 200, LinkState::Live);
  expect_nothing_at(*source, 201, LinkState::Stalled);
}

TEST_CASE("poll: a stall is never reported before the first frame", "[replay_source]") {
  auto source = open_text(csv({row(0), row(5000)}), kTimeout, 0);
  // No poll yet: NoData regardless of how much wall time has passed since open.
  CHECK(source->health().state == LinkState::NoData);
}

TEST_CASE("poll: a gap longer than kMaxGapMs is shortened to kMaxGapMs", "[replay_source]") {
  g_now_ms = 0;
  auto result = ReplaySource::from_text(csv({row(0), row(60'000), row(60'100)}), &fake_now, kTimeout);
  REQUIRE(result.source != nullptr);
  CHECK(result.summary == "<text>: 3 frames, 0 rows skipped, 10 s of drive");
  ReplaySource& source = *result.source;
  CHECK(frame_at(source, 0).t_ms == 0);
  expect_nothing_at(source, 9'999, LinkState::Stalled);
  CHECK(frame_at(source, 10'000).t_ms == 60'000);
  CHECK(frame_at(source, 10'100).t_ms == 60'100);
}

TEST_CASE("poll: a gap of exactly kMaxGapMs is not shortened", "[replay_source]") {
  auto source = open_text(csv({row(0), row(10'000)}), kTimeout, 0);
  static_cast<void>(frame_at(*source, 0));
  CHECK_FALSE(poll_at(*source, 9'999).has_value());
  CHECK(frame_at(*source, 10'000).t_ms == 10'000);
}

TEST_CASE("poll: several long gaps are each capped, so the drive is bounded", "[replay_source]") {
  g_now_ms = 0;
  auto result = ReplaySource::from_text(csv({row(0), row(100'000), row(200'000), row(300'000)}), &fake_now, kTimeout);
  REQUIRE(result.source != nullptr);
  CHECK_THAT(result.summary, EndsWith("30 s of drive"));
  ReplaySource& source = *result.source;
  static_cast<void>(frame_at(source, 0));
  CHECK(frame_at(source, 10'000).t_ms == 100'000);
  CHECK(frame_at(source, 20'000).t_ms == 200'000);
  CHECK(frame_at(source, 30'000).t_ms == 300'000);
}

TEST_CASE("poll: a backwards MCU timestamp (restart) plays immediately", "[replay_source]") {
  // The recorder kept running across an ignition cycle; the MCU clock restarted at 0.
  auto source = open_text(csv({row(1000), row(2000), row(50), row(150)}), kTimeout, 0);
  CHECK(frame_at(*source, 0).t_ms == 1000);
  CHECK(frame_at(*source, 1000).t_ms == 2000);
  CHECK(frame_at(*source, 1000).t_ms == 50);  // no wait for the restart row
  CHECK_FALSE(poll_at(*source, 1099).has_value());
  CHECK(frame_at(*source, 1100).t_ms == 150);  // spacing resumes from the restarted clock
}

TEST_CASE("poll: equal timestamps are released back to back", "[replay_source]") {
  auto source = open_text(csv({row(100), row(100), row(200)}), kTimeout, 0);
  CHECK(frame_at(*source, 0).t_ms == 100);
  CHECK(frame_at(*source, 0).t_ms == 100);
  CHECK_FALSE(poll_at(*source, 0).has_value());
  CHECK(frame_at(*source, 100).t_ms == 200);
}

// ---------------------------------------------------------------------------
// poll: end of recording
// ---------------------------------------------------------------------------

TEST_CASE("poll: the poll that returns the last frame still reports Live", "[replay_source]") {
  auto source = open_text(csv({row(0), row(100)}), kTimeout, 0);
  static_cast<void>(frame_at(*source, 0));
  CHECK_FALSE(source->finished());
  CHECK(frame_at(*source, 100).t_ms == 100);
  CHECK(source->finished());
  CHECK(source->health().state == LinkState::Live);
}

TEST_CASE("poll: after the last frame the link is Failed and stays Failed", "[replay_source]") {
  auto source = open_text(csv({row(0), row(100)}), kTimeout, 0);
  static_cast<void>(frame_at(*source, 0));
  static_cast<void>(frame_at(*source, 100));
  const std::uint64_t last_wall = source->health().last_frame_monotonic_ms;

  expect_nothing_at(*source, 101, LinkState::Failed);
  expect_nothing_at(*source, 5000, LinkState::Failed);
  expect_nothing_at(*source, 600'000, LinkState::Failed);
  const SourceHealth h = source->health();
  CHECK(h.frames_ok == 2);
  CHECK(h.last_frame_monotonic_ms == last_wall);
  CHECK(source->finished());
}

TEST_CASE("poll: a single-row recording plays its frame and then fails", "[replay_source]") {
  auto source = open_text(csv({row(42)}), kTimeout, 0);
  CHECK(source->frame_count() == 1);
  CHECK(frame_at(*source, 0).t_ms == 42);
  CHECK(source->finished());
  expect_nothing_at(*source, 0, LinkState::Failed);
}

TEST_CASE("poll: Failed takes precedence over Stalled once the file has ended", "[replay_source]") {
  auto source = open_text(csv({row(0)}), kTimeout, 0);
  static_cast<void>(frame_at(*source, 0));
  expect_nothing_at(*source, 50'000, LinkState::Failed);  // long past the stall timeout, but it is the end
}

// ---------------------------------------------------------------------------
// poll: clock anomalies
// ---------------------------------------------------------------------------

TEST_CASE("poll: a clock that steps backwards re-bases the timeline without replaying frames", "[replay_source]") {
  auto source = open_text(csv({row(0), row(1000), row(2000)}), kTimeout, 10'000);
  CHECK(frame_at(*source, 10'000).t_ms == 0);
  CHECK(frame_at(*source, 11'000).t_ms == 1000);

  // Wall clock jumps back to 500: the third row (offset 2000) is now due at 2500.
  CHECK_FALSE(poll_at(*source, 500).has_value());
  CHECK_FALSE(poll_at(*source, 2499).has_value());
  CHECK(frame_at(*source, 2500).t_ms == 2000);
  CHECK(source->health().frames_ok == 3);
  CHECK(source->finished());
}

TEST_CASE("poll: polling faster than the recording never duplicates or drops a frame", "[replay_source]") {
  auto source = open_text(csv({row(0), row(100), row(200), row(300)}), kTimeout, 0);
  std::vector<std::uint32_t> seen;
  for (std::uint64_t now = 0; now <= 400; now += 7) {
    if (const auto f = poll_at(*source, now)) {
      seen.push_back(f->t_ms);
    }
  }
  CHECK(seen == std::vector<std::uint32_t>{0, 100, 200, 300});
}

}  // namespace
