// TelemetryIngestor — moves frames from the SensorSource to the two things that consume them.
//
// Recording consumes every accepted frame; presentation consumes only the latest snapshot. So
// pump() drains the source, hands each frame to the recorder, and publishes it to a triple
// buffer; update() takes the newest snapshot for the renderer; view() turns it into the link
// state the scene draws from. Stale-out (safety invariant 4) is decided here from the frame's
// arrival time against the injected clock, never from the frame's own timestamp.
//
// Threads: pump() is the producer and update()/view() the consumer of a single-producer,
// single-consumer buffer. They may run on two threads or on one; no locks either way. Hot path:
// no allocation, no exceptions.
#pragma once

#include "telemetry/freshness.hpp"
#include "telemetry/sensor_source.hpp"
#include "telemetry/snapshot.hpp"
#include "utils/clock.hpp"

#include <cstdint>

namespace c3::app {

class TelemetryIngestor {
public:
  /// Frames drained per pump(): bounds the work of one call after a long pause.
  static constexpr unsigned kMaxFramesPerPump = 32;

  /// `source` must outlive the ingestor. `clock` stamps each frame's arrival.
  explicit TelemetryIngestor(telemetry::SensorSource& source,
                             utils::MonotonicClock clock = utils::steady_now_ms) noexcept
      : clock_(clock), source_(source) {}

  /// Drains up to kMaxFramesPerPump frames from the source. Every frame goes to `record`
  /// (`void(const telemetry::Frame&)`), in order; the snapshot keeps only the newest.
  template <typename Record>
  void pump(Record&& record) noexcept {
    for (unsigned i = 0; i < kMaxFramesPerPump; ++i) {
      const auto frame = source_.poll();
      if (!frame) {
        break;
      }
      record(*frame);
      snapshot_.publish({.frame = *frame, .received_ms = clock_()});
    }
  }

  /// Adopts the newest snapshot since the last call. Consumer side; call once per tick before
  /// view().
  ///
  /// @return true if a new frame was adopted; otherwise false and the previous one stands
  [[nodiscard]] bool update() noexcept;

  /// The latest frame and the link state for it. Failed if the source says so; NoData before
  /// the first frame; Live while the frame is younger than `timeout_ms` (capped at
  /// kMaxStallTimeoutMs); otherwise Stalled. `age_ms` is 0 without a frame.
  ///
  /// The frame pointer refers to storage inside the ingestor and stays valid until the next
  /// update().
  [[nodiscard]] telemetry::TelemetryView view(std::uint32_t timeout_ms) const noexcept;

private:
  utils::MonotonicClock clock_;
  telemetry::SensorSource& source_;
  telemetry::FrameSnapshot snapshot_;
  telemetry::TimedFrame latest_{};  // the frame the renderer sees, with its arrival time
  bool have_frame_ = false;
};

}  // namespace c3::app
