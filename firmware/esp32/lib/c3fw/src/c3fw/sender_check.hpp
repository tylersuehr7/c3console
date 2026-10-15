// Range validation (spec §3.4): "out-of-range reads a sender fault, not a value".
//
// With the piggyback read (spec §3.2) an open sender lets the node rise to the gauge supply,
// so it reads near full scale; a short pulls the node to ground. Whether a short can be told
// from a legitimate reading depends on the sender: a GM fuel float is 0 Ω at empty and the oil
// sender ~1 Ω at 0 psi, so for those channels a short is indistinguishable from the low end
// and is not flagged (0 psi already raises OIL_LOW, which is the safe direction). The windows
// are bench-confirmed constants; until then these are starting points, like the ranges in §3.1.
#pragma once

#include "telemetry/frame.hpp"

#include <array>
#include <cstdint>

namespace c3fw {

enum class SenderState : std::uint8_t {
  Ok,
  Open,
  Short,
};

struct SenderWindow {
  bool detect_open = true;
  std::int32_t open_above = 31784;  // 0.97 × full scale
  bool detect_short = false;
  std::int32_t short_below = 327;  // 0.01 × full scale
};

[[nodiscard]] constexpr SenderState check_sender(std::int32_t raw, const SenderWindow& w) noexcept {
  if (w.detect_open && raw > w.open_above) {
    return SenderState::Open;
  }

  if (w.detect_short && raw < w.short_below) {
    return SenderState::Short;
  }

  return SenderState::Ok;
}

/// Per-channel defaults, indexed by telemetry::Channel.
[[nodiscard]] constexpr std::array<SenderWindow, c3::telemetry::kChannelCount> default_sender_windows() noexcept {
  return {
      SenderWindow{.detect_open = true, .detect_short = false},   // fuel
      SenderWindow{.detect_open = true, .detect_short = false},   // oil
      SenderWindow{.detect_open = true, .detect_short = true},    // coolant
      SenderWindow{.detect_open = false, .detect_short = false},  // volt: VOLT_LOW covers it
  };
}

}  // namespace c3fw
