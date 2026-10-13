#pragma once

#include "sensor_source.hpp"
#include "utils/triple_buffer.hpp"

#include <cstdint>
#include <type_traits>

namespace c3::telemetry {

struct TelemetryView {
  const Frame* frame = nullptr;
  LinkState link = LinkState::NoData;
  std::uint64_t age_ms = 0;

  [[nodiscard]] bool live() const noexcept { return frame != nullptr && link == LinkState::Live; }
};

struct TimedFrame {
  Frame frame{};
  std::uint64_t received_ms = 0;
};
static_assert(std::is_trivially_copyable_v<TimedFrame>);

using FrameSnapshot = utils::TripleBuffer<TimedFrame>;

}  // namespace c3::telemetry
