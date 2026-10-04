// Telemetry line protocol, MCU -> Pi (spec §4.1): newline-delimited JSON at 10 Hz.
//
//   {"t":184523,"fuel":0.62,"oil":41.2,"clt":196.4,"volt":14.1,
//    "faults":["OIL_LOW"],"raw":{"fuel":12843,"oil":8211,"clt":19004,"volt":21877},
//    "relays":{"blower":2,"ac":true,"mode":"vent","headlights":false},"crc":"a3f1"}
//
// Hand-rolled, fixed-buffer, no allocation, no exceptions.
#pragma once

#include "frame.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace c3::telemetry {

/// Longest line accepted, without the newline. Generous: a full frame is ~170 bytes.
constexpr std::size_t kMaxLineLength = 512;

enum class ParseError : std::uint8_t {
  None,
  Empty,
  TooLong,
  MissingCrc,     // no `,"crc":"…"` field
  BadCrcField,    // crc present but not 4 hex digits, or not the last field
  CrcMismatch,    // counted separately (acceptance: "CRC failures are counted and dropped")
  Malformed,      // not the JSON shape expected
  MissingField,   // a required field is abset
  OutOfRange,     // value outside wire plausibility limits
  UnknownFault,   // a string in faults[] is not a known code
  TooManyFaults,  // more than Frame can hold
};

[[nodiscard]] constexpr std::string_view to_string(ParseError e) noexcept {
  switch (e) {
    case ParseError::None:
      return "ok";
    case ParseError::Empty:
      return "empty";
    case ParseError::TooLong:
      return "too long";
    case ParseError::MissingCrc:
      return "missing crc";
    case ParseError::BadCrcField:
      return "bad crc field";
    case ParseError::CrcMismatch:
      return "crc mismatch";
    case ParseError::Malformed:
      return "malformed";
    case ParseError::MissingField:
      return "missing field";
    case ParseError::OutOfRange:
      return "out of range";
    case ParseError::UnknownFault:
      return "unknown fault";
    case ParseError::TooManyFaults:
      return "too many faults";
  }
  return "?";
}

struct ParseResult {
  std::optional<Frame> frame;  // set iff error == None
  ParseError error = ParseError::None;
};

/// Maps the raw-object keys ("fuel", "oil", "clt", "volt") to channels.
[[nodiscard]] std::optional<Channel> parse_channel_key(std::string_view key) noexcept;

/// Parses one line (no trailing newline; a trailing '\r' is tolerated).
[[nodiscard]] ParseResult parse_frame(std::string_view line) noexcept;

/// Reassembles lines from a byte stream. Overly long lines and anything before the
/// first newline after them are discarded and counted so garbage mid-stream costs at
/// most the frame it landed in.
class LineReader {
public:
  template <typename OnLine>
  void feed(std::span<const char> bytes, OnLine&& on_line) noexcept {
    for (const char c : bytes) {
      if (c == '\n') {
        if (!overflow_) {
          on_line(std::string_view(buf_.data(), len_));
        } else {
          ++dropped_;
        }
        len_ = 0;
        overflow_ = false;
        continue;
      }

      if (overflow_) {
        continue;
      }

      if (len_ >= kMaxLineLength) {
        overflow_ = true;
        continue;
      }

      buf_[len_++] = c;
    }
  }

  /// Lines discarded for exceeding kMaxLineLength.
  [[nodiscard]] std::uint32_t dropped() const noexcept { return dropped_; }
  [[nodiscard]] std::size_t pending() const noexcept { return len_; }

private:
  std::array<char, kMaxLineLength> buf_{};
  std::size_t len_ = 0;
  std::uint32_t dropped_ = 0;
  bool overflow_ = false;
};

}  // namespace c3::telemetry