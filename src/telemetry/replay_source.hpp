// ReplaySource — plays a recorded CSV drive back at its original cadence.
//
// The file is read and validated at init; rows that do not parse are counted as malformed,
// reported with their line number in Result::warnings and skipped. poll() then releases one frame
// each time its recorded timestamp comes due against the injected clock, so gaps in the recording
// replay as stalls (gauges grey out exactly as they did on the road). A gap longer than kMaxGapMs
// is shortened to that, so a recording that spans an ignition cycle does not park the replay. After
// the last row the link reports Failed: the file ended, there is nothing live to show.
#pragma once

#include "frame.hpp"
#include "sensor_source.hpp"
#include "utils/clock.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace c3::telemetry {

class ReplaySource final : public SensorSource {
public:
  static constexpr std::uint64_t kMaxGapMs = 10'000;

  struct Result {
    std::unique_ptr<ReplaySource> source;  // null when `error` is set
    std::string error;
    std::vector<std::string> warnings;  // one per skipped row: "<name>:<line>: <reason>"
    std::string summary;                // frame count, skipped rows, duration; for the log
  };

  /// Reads and parses the file. Init-time: allocates the frame list.
  [[nodiscard]] static Result open(const std::string& path, utils::MonotonicClock clock,
                                   std::uint32_t stall_timeout_ms);
  /// Same, from text already in memory (tests, embedded fixtures).
  [[nodiscard]] static Result from_text(std::string_view csv, utils::MonotonicClock clock,
                                        std::uint32_t stall_timeout_ms, std::string_view name = "<text>");

  [[nodiscard]] std::optional<Frame> poll() noexcept override;
  [[nodiscard]] SourceHealth health() const noexcept override;

  [[nodiscard]] std::size_t frame_count() const noexcept { return frames_.size(); }
  [[nodiscard]] std::size_t rows_skipped() const noexcept { return skipped_; }
  [[nodiscard]] bool finished() const noexcept { return next_ >= frames_.size(); }

private:
  struct Row {
    Frame frame;
    std::uint64_t due_ms = 0;  // offset from the first frame, gaps capped
  };

  ReplaySource(std::vector<Row> rows, std::size_t skipped, utils::MonotonicClock clock,
               std::uint32_t stall_timeout_ms) noexcept;

  std::vector<Row> frames_;
  std::size_t skipped_;
  utils::MonotonicClock clock_;
  std::uint32_t stall_timeout_ms_;
  bool started_ = false;
  std::uint64_t start_ms_ = 0;
  std::size_t next_ = 0;
  SourceHealth health_{};
};

}  // namespace c3::telemetry
