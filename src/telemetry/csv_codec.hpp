// CSV row codec for the replay corpus (spec §9.3):
//   timestamp_ms,fuel_raw,oil_raw,clt_raw,volt_raw,fuel,oil,clt,volt,faults
//
// timestamp_ms is the MCU's `t`. faults is the frame's codes joined with '|' (the spec leaves
// the delimiter open; recorded in docs/milestones.md), empty when there are none. No quoting
// anywhere: every field is a number or a code list. Allocation-free both ways so the recorder
// can run on the telemetry path and the replay source can parse at init or on poll.
#pragma once

#include "frame.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace c3::telemetry {

constexpr std::string_view kCsvHeader = "timestamp_ms,fuel_raw,oil_raw,clt_raw,volt_raw,fuel,oil,clt,volt,faults";
constexpr char kCsvFaultDelimiter = '|';
constexpr std::size_t kMaxCsvRowLength = 256;  // longest row: 8 sender faults, full precision

enum class CsvError : std::uint8_t {
  None,
  FieldCount,  // not exactly 10 comma-separated fields
  BadNumber,
  OutOfRange,  // beyond telemetry/limits.h
  UnknownFault,
  TooManyFaults,
};

[[nodiscard]] constexpr std::string_view to_string(CsvError error) noexcept {
  switch (error) {
    case CsvError::None:
      return "ok";
    case CsvError::FieldCount:
      return "wrong field count";
    case CsvError::BadNumber:
      return "bad number";
    case CsvError::OutOfRange:
      return "value out of range";
    case CsvError::UnknownFault:
      return "unknown fault code";
    case CsvError::TooManyFaults:
      return "too many faults";
  }
  return "?";
}

/// Writes one row without a line terminator. Returns the length, or 0 if `out` is too small.
[[nodiscard]] std::size_t format_csv_row(const Frame& frame, std::span<char> out) noexcept;

/// Parses one row (no terminator; a trailing '\r' is tolerated). On success `out` carries
/// t_ms, values, raw counts and faults; relays stay empty and reverse false (not recorded).
[[nodiscard]] CsvError parse_csv_row(std::string_view row, Frame& out) noexcept;

/// True when `line` is the header row (exact match after trimming a trailing '\r').
[[nodiscard]] bool is_csv_header(std::string_view line) noexcept;

}  // namespace c3::telemetry
