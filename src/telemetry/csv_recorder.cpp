#include "csv_recorder.hpp"

#include "csv_codec.hpp"

#include <array>
#include <utility>

namespace c3::telemetry {

CsvRecorder::CsvRecorder(UniqueFile file, std::string path) noexcept : file_(std::move(file)), path_(std::move(path)) {}

CsvRecorder::Result CsvRecorder::open(const std::string& path) {
  UniqueFile file(std::fopen(path.c_str(), "w"));

  if (!file) {
    return {.recorder = nullptr, .error = "record: cannot create " + path};
  }

  if (std::fputs(kCsvHeader.data(), file.get()) < 0 || std::fputc('\n', file.get()) == EOF ||
      std::fflush(file.get()) != 0) {
    return {.recorder = nullptr, .error = "record: cannot write to " + path};
  }

  return {.recorder = std::unique_ptr<CsvRecorder>(new CsvRecorder(std::move(file), path)), .error = {}};
}

void CsvRecorder::record(const Frame& frame) noexcept {
  std::array<char, kMaxCsvRowLength> row{};
  const std::size_t n = format_csv_row(frame, row);
  row[n] = '\n';
  const bool ok = n > 0 && std::fwrite(row.data(), 1, n + 1, file_.get()) == n + 1 && std::fflush(file_.get()) == 0;
  if (ok) {
    ++written_;
  } else {
    ++errors_;  // reported by the app at shutdown; the display never depends on recording
  }
}

}  // namespace c3::telemetry
