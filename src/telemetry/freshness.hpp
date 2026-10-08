#pragma once

#include "sensor_source.hpp"

#include <algorithm>
#include <cstdint>

namespace c3::telemetry {

constexpr std::uint32_t kMaxStallTimeoutMs = 1000;

[[nodiscard]] constexpr std::uint64_t frame_age(std::uint64_t now, std::uint64_t received) noexcept {
  return now >= received ? now - received : kMaxStallTimeoutMs;
}

[[nodiscard]] constexpr LinkState link_state(bool failed, bool have_frame, std::uint64_t now, std::uint64_t received,
                                             std::uint32_t timeout) noexcept {
  if (failed) {
    return LinkState::Failed;
  }

  if (!have_frame) {
    return LinkState::NoData;
  }

  return frame_age(now, received) < std::min(timeout, kMaxStallTimeoutMs) ? LinkState::Live : LinkState::Stalled;
}

}  // namespace c3::telemetry
