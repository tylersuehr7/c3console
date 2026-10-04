#pragma once

#include "frame.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace c3::telemetry {

/// True for the kinds that carry a channel suffix.
[[nodiscard]] constexpr bool has_channel(FaultKind kind) noexcept {
  return kind == FaultKind::SenderOpen || kind == FaultKind::SenderShort;
}

/// "SENDER_SHORT_FUEL" and "SENDER_SHORT_VOLT" are the longest fault codes.
constexpr std::size_t kMaxFaultCodeLength = 17;

[[nodiscard]] std::optional<Channel> parse_channel(std::string_view text) noexcept;

/// "SENDER_OPEN_OIL" -> {SenderOpen, Oil}; "OIL_LOW" -> {OilLow, {}}. nullopt if unknown.
[[nodiscard]] std::optional<Fault> parse_fault_code(std::string_view code) noexcept;

/// Writes the code into `out` without a terminator.
///
/// @return the length, or 0 if `out` is too small (sizes on `kMaxFaultCodeLength`).
[[nodiscard]] std::size_t format_fault_code(Fault fault, std::span<char> out) noexcept;

}  // namespace c3::telemetry