// Serialises the telemetry frame (spec §4.1) and command acknowledgements (spec §4.2) into
// fixed buffers, CRC included. Uses the Pi's own Crc16Ccitt so both ends cannot disagree on
// the variant, and lays the line out exactly as telemetry/protocol.cpp verifies it: the CRC is
// computed over the object serialised without the crc member, then `,"crc":"xxxx"` is
// spliced in before the closing brace. tests/test_firmware_codec.cpp round-trips both.
#pragma once

#include "telemetry/frame.hpp"

#include <cstddef>
#include <span>
#include <string_view>

namespace c3fw {

/// Writes the frame line followed by '\n'. Returns the length, or 0 if `out` is too small.
/// `relays` should be set on the frame: the MCU always re-asserts its relay state. An unset
/// optional is written as the all-off default.
[[nodiscard]] std::size_t write_frame(const c3::telemetry::Frame& f, std::span<char> out) noexcept;

/// {"ack":"<cmd>","ok":true}\n or {"ack":"<cmd>","ok":false,"err":"<reason>"}\n.
/// `cmd` is echoed only when it is a plain token; anything else is written as "?". `err` is
/// expected to be one of the parser's static strings.
[[nodiscard]] std::size_t write_ack(std::string_view cmd, bool ok, std::string_view err, std::span<char> out) noexcept;

}  // namespace c3fw
