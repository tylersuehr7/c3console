#include "telemetry/csv_codec.hpp"
#include "telemetry/csv_recorder.hpp"
#include "telemetry/frame.hpp"
#include "vehicle/vehicle_types.hpp"

#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

using c3::telemetry::Channel;
using c3::telemetry::CsvError;
using c3::telemetry::CsvRecorder;
using c3::telemetry::Fault;
using c3::telemetry::FaultKind;
using c3::telemetry::format_csv_row;
using c3::telemetry::Frame;
using c3::telemetry::is_csv_header;
using c3::telemetry::kCsvHeader;
using c3::telemetry::kMaxCsvRowLength;
using c3::telemetry::parse_csv_row;

namespace {

/// A fresh, empty directory under the system temp root, removed when the test case ends.
class ScratchDir {
public:
  ScratchDir() {
    static std::atomic<unsigned> counter{0};
    const auto name = "c3console_csv_recorder_" + std::to_string(::getpid()) + "_" + std::to_string(counter++);
    path_ = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(path_);
    REQUIRE(std::filesystem::create_directory(path_));
  }
  ~ScratchDir() { std::filesystem::remove_all(path_); }
  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;
  ScratchDir(ScratchDir&&) = delete;
  ScratchDir& operator=(ScratchDir&&) = delete;

  [[nodiscard]] std::string file(std::string_view name) const { return (path_ / name).string(); }
  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
  std::filesystem::path path_;
};

[[nodiscard]] std::string read_all(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.is_open());
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

/// Splits file content on '\n'. A trailing newline does not produce an empty final element.
[[nodiscard]] std::vector<std::string> lines_of(const std::string& content) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (start < content.size()) {
    const std::size_t nl = content.find('\n', start);
    if (nl == std::string::npos) {
      lines.push_back(content.substr(start));
      break;
    }
    lines.push_back(content.substr(start, nl - start));
    start = nl + 1;
  }
  return lines;
}

[[nodiscard]] std::string row_of(const Frame& frame) {
  std::array<char, kMaxCsvRowLength> row{};
  const std::size_t n = format_csv_row(frame, row);
  REQUIRE(n > 0);
  return std::string(row.data(), n);
}

[[nodiscard]] Frame sample_frame(std::uint32_t t) noexcept {
  Frame f;
  f.t_ms = t;
  f.raw = {12843, 8211, 19004, 21877};
  f.fuel_fraction = 0.62F;
  f.oil_psi = 41.2F;
  f.coolant_f = 196.4F;
  f.voltage_v = 14.1F;
  return f;
}

/// A frame whose row cannot fit kMaxCsvRowLength: widest numerals plus a full fault list.
/// The recorder does not validate frames (that happened upstream); it must refuse and count.
[[nodiscard]] Frame unwritable_frame(std::uint32_t t) {
  Frame f;
  f.t_ms = t;
  f.raw.fill(std::numeric_limits<std::int32_t>::min());
  f.fuel_fraction = -3e38F;
  f.oil_psi = -3e38F;
  f.coolant_f = -3e38F;
  f.voltage_v = -3e38F;
  for (const auto channel : {Channel::Fuel, Channel::Oil, Channel::Coolant, Channel::Volt}) {
    REQUIRE(f.faults.push_back(Fault{.kind = FaultKind::SenderOpen, .channel = channel}));
    REQUIRE(f.faults.push_back(Fault{.kind = FaultKind::SenderShort, .channel = channel}));
  }
  std::array<char, kMaxCsvRowLength> probe{};
  REQUIRE(format_csv_row(f, probe) == 0);
  return f;
}

[[nodiscard]] std::unique_ptr<CsvRecorder> open_or_fail(const std::string& path) {
  auto result = CsvRecorder::open(path);
  INFO("error: " << result.error);
  REQUIRE(result.recorder != nullptr);
  REQUIRE(result.error.empty());
  return std::move(result.recorder);
}

// ---------------------------------------------------------------------------
// Type properties
// ---------------------------------------------------------------------------

TEST_CASE("CsvRecorder: owns its file and cannot be copied", "[csv_recorder]") {
  STATIC_CHECK_FALSE(std::is_copy_constructible_v<CsvRecorder>);
  STATIC_CHECK_FALSE(std::is_copy_assignable_v<CsvRecorder>);
  STATIC_CHECK(noexcept(std::declval<CsvRecorder&>().record(std::declval<const Frame&>())));
}

// ---------------------------------------------------------------------------
// open
// ---------------------------------------------------------------------------

TEST_CASE("open: creates the file and writes only the header", "[csv_recorder]") {
  const ScratchDir dir;
  const std::string path = dir.file("drive.csv");

  const auto recorder = open_or_fail(path);
  CHECK(recorder->path() == path);
  CHECK(recorder->frames_written() == 0);
  CHECK(recorder->write_errors() == 0);

  const auto lines = lines_of(read_all(path));
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == kCsvHeader);
  CHECK(is_csv_header(lines[0]));
  CHECK(read_all(path) == std::string(kCsvHeader) + "\n");
}

TEST_CASE("open: the header is flushed before open returns", "[csv_recorder]") {
  const ScratchDir dir;
  const std::string path = dir.file("drive.csv");
  const auto recorder = open_or_fail(path);
  // Read while the recorder is still alive and holding the handle.
  CHECK(std::filesystem::file_size(path) == kCsvHeader.size() + 1);
}

TEST_CASE("open: truncates an existing file", "[csv_recorder]") {
  const ScratchDir dir;
  const std::string path = dir.file("drive.csv");
  {
    const auto first = open_or_fail(path);
    first->record(sample_frame(1));
    first->record(sample_frame(2));
    REQUIRE(first->frames_written() == 2);
  }
  REQUIRE(lines_of(read_all(path)).size() == 3);

  const auto second = open_or_fail(path);
  CHECK(second->frames_written() == 0);
  const auto lines = lines_of(read_all(path));
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == kCsvHeader);
}

TEST_CASE("open: a path in a missing directory fails with no recorder and a reason", "[csv_recorder]") {
  const ScratchDir dir;
  const std::string path = dir.file("missing/subdir/drive.csv");
  const auto result = CsvRecorder::open(path);
  CHECK(result.recorder == nullptr);
  CHECK_FALSE(result.error.empty());
  CHECK(result.error.find("cannot create") != std::string::npos);
  CHECK(result.error.find(path) != std::string::npos);
  CHECK_FALSE(std::filesystem::exists(path));
}

TEST_CASE("open: a path that is a directory fails", "[csv_recorder]") {
  const ScratchDir dir;
  const auto result = CsvRecorder::open(dir.path().string());
  CHECK(result.recorder == nullptr);
  CHECK(result.error.find("cannot create") != std::string::npos);
}

TEST_CASE("open: the error message is prefixed for the app's log", "[csv_recorder]") {
  const auto result = CsvRecorder::open("/nonexistent-c3console-dir/drive.csv");
  REQUIRE(result.recorder == nullptr);
  CHECK(result.error.rfind("record: ", 0) == 0);
}

TEST_CASE("open: two recorders may write different files at once", "[csv_recorder]") {
  const ScratchDir dir;
  const auto a = open_or_fail(dir.file("a.csv"));
  const auto b = open_or_fail(dir.file("b.csv"));
  a->record(sample_frame(1));
  b->record(sample_frame(2));
  b->record(sample_frame(3));
  CHECK(lines_of(read_all(dir.file("a.csv"))).size() == 2);
  CHECK(lines_of(read_all(dir.file("b.csv"))).size() == 3);
}

// ---------------------------------------------------------------------------
// record
// ---------------------------------------------------------------------------

TEST_CASE("record: appends exactly the codec row plus a newline", "[csv_recorder]") {
  const ScratchDir dir;
  const std::string path = dir.file("drive.csv");
  const auto recorder = open_or_fail(path);

  const Frame frame = sample_frame(184523);
  recorder->record(frame);
  CHECK(recorder->frames_written() == 1);
  CHECK(recorder->write_errors() == 0);

  CHECK(read_all(path) == std::string(kCsvHeader) + "\n" + row_of(frame) + "\n");
}

TEST_CASE("record: the written row is the spec example text", "[csv_recorder]") {
  const ScratchDir dir;
  const std::string path = dir.file("drive.csv");
  const auto recorder = open_or_fail(path);
  recorder->record(sample_frame(184523));
  const auto lines = lines_of(read_all(path));
  REQUIRE(lines.size() == 2);
  CHECK(lines[1] == "184523,12843,8211,19004,21877,0.6200,41.20,196.40,14.100,");
}

TEST_CASE("record: rows land in call order and are counted", "[csv_recorder]") {
  const ScratchDir dir;
  const std::string path = dir.file("drive.csv");
  const auto recorder = open_or_fail(path);

  constexpr std::uint32_t kCount = 50;
  for (std::uint32_t i = 0; i < kCount; ++i) {
    recorder->record(sample_frame(100 * i));
  }
  CHECK(recorder->frames_written() == kCount);
  CHECK(recorder->write_errors() == 0);

  const auto lines = lines_of(read_all(path));
  REQUIRE(lines.size() == kCount + 1);
  for (std::uint32_t i = 0; i < kCount; ++i) {
    Frame parsed;
    REQUIRE(parse_csv_row(lines[i + 1], parsed) == CsvError::None);
    CHECK(parsed.t_ms == 100 * i);
  }
}

TEST_CASE("record: every frame is flushed so the file is readable while recording", "[csv_recorder]") {
  const ScratchDir dir;
  const std::string path = dir.file("drive.csv");
  const auto recorder = open_or_fail(path);

  for (std::uint32_t i = 1; i <= 5; ++i) {
    recorder->record(sample_frame(i));
    const auto lines = lines_of(read_all(path));
    REQUIRE(lines.size() == i + 1);
    CHECK(lines.back() == row_of(sample_frame(i)));
  }
}

TEST_CASE("record: the file always ends with a newline and never a partial row", "[csv_recorder]") {
  const ScratchDir dir;
  const std::string path = dir.file("drive.csv");
  const auto recorder = open_or_fail(path);
  recorder->record(sample_frame(1));
  recorder->record(sample_frame(2));
  const std::string content = read_all(path);
  REQUIRE_FALSE(content.empty());
  CHECK(content.back() == '\n');
  CHECK(content.find("\n\n") == std::string::npos);
  CHECK(content.find('\r') == std::string::npos);
}

TEST_CASE("record: faults are written as the pipe-joined code list", "[csv_recorder]") {
  const ScratchDir dir;
  const std::string path = dir.file("drive.csv");
  const auto recorder = open_or_fail(path);

  Frame frame = sample_frame(7);
  REQUIRE(frame.faults.push_back(Fault{.kind = FaultKind::OilLow}));
  REQUIRE(frame.faults.push_back(Fault{.kind = FaultKind::SenderOpen, .channel = Channel::Coolant}));
  recorder->record(frame);

  const auto lines = lines_of(read_all(path));
  REQUIRE(lines.size() == 2);
  CHECK(lines[1] == "7,12843,8211,19004,21877,0.6200,41.20,196.40,14.100,OIL_LOW|SENDER_OPEN_CLT");

  Frame parsed;
  REQUIRE(parse_csv_row(lines[1], parsed) == CsvError::None);
  REQUIRE(parsed.faults.size() == 2);
  CHECK(parsed.faults[1].kind == FaultKind::SenderOpen);
  CHECK(parsed.faults[1].channel == Channel::Coolant);
}

TEST_CASE("record: a frame with eight faults is written whole", "[csv_recorder]") {
  const ScratchDir dir;
  const std::string path = dir.file("drive.csv");
  const auto recorder = open_or_fail(path);

  Frame frame = sample_frame(9);
  for (const auto channel : {Channel::Fuel, Channel::Oil, Channel::Coolant, Channel::Volt}) {
    REQUIRE(frame.faults.push_back(Fault{.kind = FaultKind::SenderOpen, .channel = channel}));
    REQUIRE(frame.faults.push_back(Fault{.kind = FaultKind::SenderShort, .channel = channel}));
  }
  recorder->record(frame);
  CHECK(recorder->frames_written() == 1);
  CHECK(recorder->write_errors() == 0);

  const auto lines = lines_of(read_all(path));
  REQUIRE(lines.size() == 2);
  Frame parsed;
  REQUIRE(parse_csv_row(lines[1], parsed) == CsvError::None);
  CHECK(parsed.faults.size() == 8);
}

TEST_CASE("record: relays and reverse are not recorded", "[csv_recorder]") {
  const ScratchDir dir;
  const std::string path = dir.file("drive.csv");
  const auto recorder = open_or_fail(path);

  Frame plain = sample_frame(1);
  Frame busy = sample_frame(1);
  busy.reverse = true;
  busy.relays = c3::vehicle::RelayState{.blower = c3::vehicle::HvacBlower::L3, .ac_enabled = true};
  recorder->record(plain);
  recorder->record(busy);

  const auto lines = lines_of(read_all(path));
  REQUIRE(lines.size() == 3);
  CHECK(lines[1] == lines[2]);
}

TEST_CASE("record: every written row parses back to the frame that was recorded", "[csv_recorder]") {
  const ScratchDir dir;
  const std::string path = dir.file("drive.csv");
  const auto recorder = open_or_fail(path);

  std::vector<Frame> frames;
  for (std::uint32_t i = 0; i < 10; ++i) {
    Frame f = sample_frame(1000 + i * 100);
    f.fuel_fraction = static_cast<float>(i) / 10.0F;
    f.oil_psi = 5.0F * static_cast<float>(i);
    f.raw[0] = static_cast<std::int32_t>(i) * -1000;
    if (i % 3 == 0) {
      REQUIRE(f.faults.push_back(Fault{.kind = FaultKind::FuelLow}));
    }
    frames.push_back(f);
    recorder->record(f);
  }

  const auto lines = lines_of(read_all(path));
  REQUIRE(lines.size() == frames.size() + 1);
  for (std::size_t i = 0; i < frames.size(); ++i) {
    Frame parsed;
    REQUIRE(parse_csv_row(lines[i + 1], parsed) == CsvError::None);
    CHECK(parsed.t_ms == frames[i].t_ms);
    CHECK(parsed.raw == frames[i].raw);
    CHECK(parsed.fuel_fraction == frames[i].fuel_fraction);  // tenths are exact at %.4f
    CHECK(parsed.oil_psi == frames[i].oil_psi);
    CHECK(parsed.faults.size() == frames[i].faults.size());
  }
}

TEST_CASE("record: a frame whose row cannot be formatted is counted as an error and not written", "[csv_recorder]") {
  const ScratchDir dir;
  const std::string path = dir.file("drive.csv");
  const auto recorder = open_or_fail(path);
  recorder->record(sample_frame(1));
  const std::string before = read_all(path);

  const Frame huge = unwritable_frame(2);
  recorder->record(huge);
  CHECK(recorder->frames_written() == 1);
  CHECK(recorder->write_errors() == 1);
  CHECK(read_all(path) == before);

  recorder->record(sample_frame(3));
  CHECK(recorder->frames_written() == 2);
  CHECK(recorder->write_errors() == 1);
  CHECK(lines_of(read_all(path)).size() == 3);
}

TEST_CASE("record: counters are independent and monotonic", "[csv_recorder]") {
  const ScratchDir dir;
  const auto recorder = open_or_fail(dir.file("drive.csv"));
  const Frame bad = unwritable_frame(0);

  recorder->record(bad);
  recorder->record(sample_frame(1));
  recorder->record(bad);
  recorder->record(sample_frame(2));
  recorder->record(sample_frame(3));
  CHECK(recorder->frames_written() == 3);
  CHECK(recorder->write_errors() == 2);
}

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

TEST_CASE("destructor: closes the file and leaves the content intact", "[csv_recorder]") {
  const ScratchDir dir;
  const std::string path = dir.file("drive.csv");
  std::string expected;
  {
    const auto recorder = open_or_fail(path);
    recorder->record(sample_frame(1));
    recorder->record(sample_frame(2));
    expected = std::string(kCsvHeader) + "\n" + row_of(sample_frame(1)) + "\n" + row_of(sample_frame(2)) + "\n";
  }
  CHECK(read_all(path) == expected);
  // The handle is gone: the file can be removed and recreated freely.
  CHECK(std::filesystem::remove(path));
  CHECK_FALSE(std::filesystem::exists(path));
}

TEST_CASE("open/record/replay: a recorded file is a valid replay corpus", "[csv_recorder]") {
  const ScratchDir dir;
  const std::string path = dir.file("drive.csv");
  {
    const auto recorder = open_or_fail(path);
    for (std::uint32_t t = 0; t < 2000; t += 100) {
      Frame f = sample_frame(t);
      if (t == 700) {
        REQUIRE(f.faults.push_back(Fault{.kind = FaultKind::OilLow}));
      }
      recorder->record(f);
    }
  }

  const auto lines = lines_of(read_all(path));
  REQUIRE_FALSE(lines.empty());
  REQUIRE(is_csv_header(lines[0]));

  std::uint32_t last_t = 0;
  std::size_t rows = 0;
  std::size_t faulted = 0;
  for (std::size_t i = 1; i < lines.size(); ++i) {
    Frame f;
    REQUIRE(parse_csv_row(lines[i], f) == CsvError::None);
    if (rows > 0) {
      CHECK(f.t_ms > last_t);
    }
    last_t = f.t_ms;
    faulted += f.faults.empty() ? 0 : 1;
    ++rows;
  }
  CHECK(rows == 20);
  CHECK(faulted == 1);
  CHECK(last_t == 1900U);
}

}  // namespace
