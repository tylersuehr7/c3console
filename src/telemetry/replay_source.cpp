#include "replay_source.hpp"

#include "csv_codec.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace c3::telemetry {

ReplaySource::ReplaySource(std::vector<Row> rows, std::size_t skipped, utils::MonotonicClock clock,
                           std::uint32_t stall_timeout_ms) noexcept
    : frames_(std::move(rows)), skipped_(skipped), clock_(clock), stall_timeout_ms_(stall_timeout_ms) {
  health_.frames_malformed = static_cast<std::uint32_t>(skipped);
}

ReplaySource::Result ReplaySource::open(const std::string& path, utils::MonotonicClock clock,
                                        std::uint32_t stall_timeout_ms) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    Result failed;
    failed.error = path + ": cannot open";  // same shape as the parse errors: "<name>: <reason>"
    return failed;
  }

  std::ostringstream text;
  text << in.rdbuf();
  return from_text(text.str(), clock, stall_timeout_ms, path);
}

ReplaySource::Result ReplaySource::from_text(std::string_view csv, utils::MonotonicClock clock,
                                             std::uint32_t stall_timeout_ms, std::string_view name) {
  Result result;
  std::vector<Row> rows;
  std::size_t skipped = 0;
  std::size_t line_no = 0;
  bool header_seen = false;
  std::uint64_t due = 0;
  std::optional<std::uint32_t> last_t;

  while (!csv.empty()) {
    const std::size_t nl = csv.find('\n');
    const std::string_view line = csv.substr(0, nl);
    csv = nl == std::string_view::npos ? std::string_view{} : csv.substr(nl + 1);
    ++line_no;

    if (line.empty() || line == "\r") {
      continue;
    }

    if (!header_seen) {
      if (!is_csv_header(line)) {
        result.error = std::string(name) + ":1: expected the header '" + std::string(kCsvHeader) + "'";
        return result;
      }
      header_seen = true;
      continue;
    }

    Frame f;
    const CsvError err = parse_csv_row(line, f);
    if (err != CsvError::None) {
      ++skipped;
      result.warnings.push_back(std::string(name) + ":" + std::to_string(line_no) + ": " + std::string(to_string(err)) +
                                "; row skipped");
      continue;
    }

    if (last_t) {
      // Cadence from the MCU clock; a backwards step (restart) plays immediately.
      const std::uint64_t gap = f.t_ms > *last_t ? f.t_ms - *last_t : 0;
      due += std::min<std::uint64_t>(gap, kMaxGapMs);
    }

    last_t = f.t_ms;
    rows.push_back({.frame = f, .due_ms = due});
  }

  if (!header_seen) {
    result.error = std::string(name) + ": empty file";
    return result;
  }

  if (rows.empty()) {
    result.error = std::string(name) + ": no usable rows";
    return result;
  }

  result.summary = std::string(name) + ": " + std::to_string(rows.size()) + " frames, " + std::to_string(skipped) +
                   " rows skipped, " + std::to_string(due / 1000) + " s of drive";

  std::unique_ptr<ReplaySource> source(new ReplaySource(std::move(rows), skipped, clock, stall_timeout_ms));
  result.source = std::move(source);

  return result;
}

std::optional<Frame> ReplaySource::poll() noexcept {
  const std::uint64_t now = clock_();
  if (!started_ || now < start_ms_) {
    started_ = true;
    start_ms_ = now;
  }

  if (finished()) {
    health_.state = LinkState::Failed;  // the recording ended
    return std::nullopt;
  }

  const std::uint64_t elapsed = now - start_ms_;
  if (frames_[next_].due_ms > elapsed) {
    if (health_.frames_ok > 0 && now - health_.last_frame_monotonic_ms > stall_timeout_ms_) {
      health_.state = LinkState::Stalled;  // a gap in the recording replays as a stall
    }
    return std::nullopt;
  }

  const Frame& f = frames_[next_].frame;
  ++next_;
  health_.state = LinkState::Live;
  health_.last_frame_monotonic_ms = now;
  ++health_.frames_ok;

  return f;
}

SourceHealth ReplaySource::health() const noexcept {
  return health_;
}

}  // namespace c3::telemetry
