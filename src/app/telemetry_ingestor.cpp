#include "telemetry_ingestor.hpp"

namespace c3::app {

bool TelemetryIngestor::update() noexcept {
  const bool changed = snapshot_.take(latest_);
  have_frame_ |= changed;
  return changed;
}

telemetry::TelemetryView TelemetryIngestor::view(std::uint32_t timeout_ms) const noexcept {
  const auto now = clock_();
  const bool failed = source_.health().state == telemetry::LinkState::Failed;
  return {
      .frame = have_frame_ ? &latest_.frame : nullptr,
      .link = telemetry::link_state(failed, have_frame_, now, latest_.received_ms, timeout_ms),
      .age_ms = have_frame_ ? telemetry::frame_age(now, latest_.received_ms) : 0,
  };
}

}  // namespace c3::app
