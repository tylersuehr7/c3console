// Appends every accepted frame to a CSV file in the spec §9.3 format.
//
// Opened at init (the data partition on the target, anywhere on the desktop). record() formats
// into a fixed buffer and flushes stdio per frame on the background worker. Flush is not durable
// fsync; sudden power loss may also lose queued frames and OS-buffered writes. Failures are counted
// for the app to report, never thrown: recording is a convenience, the display must not depend on it.
#pragma once

#include "frame.hpp"

#include <cstdio>
#include <memory>
#include <string>

namespace c3::telemetry {

class CsvRecorder {
public:
  struct Result {
    std::unique_ptr<CsvRecorder> recorder;  // null when `error` is set
    std::string error;
  };

  /// Creates or truncates `path` and writes the header.
  [[nodiscard]] static Result open(const std::string& path);

  void record(const Frame& frame) noexcept;

  [[nodiscard]] std::uint64_t frames_written() const noexcept { return written_; }

  [[nodiscard]] std::uint32_t write_errors() const noexcept { return errors_; }

  [[nodiscard]] const std::string& path() const noexcept { return path_; }

private:
  struct FileCloser {
    void operator()(std::FILE* f) const noexcept {
      static_cast<void>(std::fclose(f));  // NOLINT(cppcoreguidelines-owning-memory): the deleter is the owner
    }
  };

  using UniqueFile = std::unique_ptr<std::FILE, FileCloser>;

  CsvRecorder(UniqueFile file, std::string path) noexcept;

  UniqueFile file_;
  std::string path_;
  std::uint64_t written_ = 0;
  std::uint32_t errors_ = 0;
};

}  // namespace c3::telemetry
