#pragma once

#include "c3fw/lookup.hpp"

#include <array>

namespace c3fw {

// A partial bench sweep must never replace the running conversion curve.
class Calibration {
public:
  static constexpr std::size_t kMinimumPoints = 8;

  [[nodiscard]] bool add(c3::telemetry::Channel channel, CalPoint point) noexcept {
    return staged_[static_cast<std::size_t>(channel)].add(point);
  }

  [[nodiscard]] const Curve* staged(c3::telemetry::Channel channel) const noexcept {
    const auto& curve = staged_[static_cast<std::size_t>(channel)];
    return curve.size() >= kMinimumPoints ? &curve : nullptr;
  }

  [[nodiscard]] bool activate(c3::telemetry::Channel channel, const Curve& curve) noexcept {
    if (curve.size() < kMinimumPoints || !Curve::from_points(curve.points())) {
      return false;
    }

    const auto i = static_cast<std::size_t>(channel);
    active_[i] = curve;
    measured_[i] = true;
    staged_[i] = {};

    return true;
  }

  [[nodiscard]] const Curve& active(c3::telemetry::Channel channel) const noexcept {
    return active_[static_cast<std::size_t>(channel)];
  }

  [[nodiscard]] bool measured(c3::telemetry::Channel channel) const noexcept {
    return measured_[static_cast<std::size_t>(channel)];
  }

private:
  std::array<Curve, c3::telemetry::kChannelCount> active_ = placeholder_curves();
  std::array<Curve, c3::telemetry::kChannelCount> staged_{};
  std::array<bool, c3::telemetry::kChannelCount> measured_{};
};

}  // namespace c3fw
