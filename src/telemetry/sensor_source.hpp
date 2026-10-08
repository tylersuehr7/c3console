// SensorSource — the seam between telemetry and the rest of the application.
//
// poll() is non-blocking and allocation-free. The event loop calls it when the source's
// descriptor is readable or on the render tick; the returned Frame is published through
// the triple-buffered snapshot, never shared by reference across threads. Every method is
// noexcept: this is the telemetry hot path, where a bad frame is a counter, not a throw.
#pragma once

#include "frame.hpp"

#include <cstdint>
#include <optional>

namespace c3::telemetry {

enum class LinkState : std::uint8_t {
  NoData,   // Nothing received yet since start
  Live,     // Frames arriving within the stall timeout
  Stalled,  // No frame for longer than stall timeout
  Failed,   // Source cannot recover on its own (device gone, file ended)
};

[[nodiscard]] constexpr std::string_view to_string(LinkState s) noexcept {
  switch (s) {
    case LinkState::NoData:
      return "no-data";
    case LinkState::Live:
      return "live";
    case LinkState::Stalled:
      return "stalled";
    case LinkState::Failed:
      return "failed";
  }
  return "?";
}

struct SourceHealth {
  LinkState state = LinkState::NoData;
  std::uint64_t last_frame_monotonic_ms = 0;  // CLOCK_MONOTONIC when the last good frame arrived
  std::uint32_t frames_ok = 0;
  std::uint32_t frames_bad_crc = 0;    // counted and dropped, never parsed
  std::uint32_t frames_malformed = 0;  // truncated, missing fields, out of range
};

class SensorSource {
public:
  virtual ~SensorSource() = default;

  SensorSource(const SensorSource&) = delete;
  SensorSource& operator=(const SensorSource&) = delete;
  SensorSource(SensorSource&&) = delete;
  SensorSource& operator=(SensorSource&&) = delete;

  /// Return the next complete, CRC-valid frame if one is available. Non-blocking.
  /// Must not allocate; a bad frame is counted in health() and skipped.
  [[nodiscard]] virtual std::optional<Frame> poll() noexcept = 0;

  /// Link state and counters. Cheap; called every render tick to decide stale-out.
  [[nodiscard]] virtual SourceHealth health() const noexcept = 0;

  /// Descriptor the event loop waits on for readability, or -1 when the source has none
  /// (mock, replay): those are polled on the render tick instead.
  [[nodiscard]] virtual int descriptor() const noexcept { return -1; }

protected:
  SensorSource() = default;
};

}  // namespace c3::telemetry
