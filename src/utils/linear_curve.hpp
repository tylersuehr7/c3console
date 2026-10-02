// A fixed-capacity piecewise linear data; end segments extrapolate for bench-limited sweeps.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

#include "fixed_vector.hpp"

namespace c3::utils {

struct CurvePoint {
  std::int32_t raw = 0;
  float value = 0.0F;
};

class LinearCurve {
 public:
  static constexpr std::size_t kCapacity = 24;

  using Points = FixedVector<CurvePoint, kCapacity>;

  constexpr LinearCurve() noexcept = default;

  constexpr LinearCurve(CurvePoint a, CurvePoint b) noexcept {
    static_cast<void>(points_.push_back(a));
    static_cast<void>(points_.push_back(b));
  }

  [[nodiscard]] static std::optional<LinearCurve> from_points(const Points& points) noexcept {
    if (points.size() < 2) {
      return std::nullopt;
    }

    for (std::size_t i = 0; i < points.size(); ++i) {
      if (!std::isfinite(points[i].value) || (i > 0 && points[i].raw <= points[i - 1].raw)) {
        return std::nullopt;
      }
    }

    LinearCurve curve;
    curve.points_ = points;
    return curve;
  }

  [[nodiscard]] bool add(CurvePoint point) noexcept {
    if (!std::isfinite(point.value)) {
      return false;
    }

    for (auto& existing : points_) {
      if (existing.raw == point.raw) {
        existing = point;
        return true;
      }
    }

    if (!points_.push_back(point)) {
      return false;
    }

    std::sort(points_.begin(), points_.end(), [](const auto& a, const auto& b) noexcept { return a.raw < b.raw; });
    return true;
  }

  [[nodiscard]] constexpr float interpolate(std::int32_t raw) const noexcept {
    if (points_.size() < 2) {
      return 0.0F;
    }

    std::size_t hi = 1;
    while (hi + 1 < points_.size() && points_[hi].raw <= raw) {
      ++hi;
    }

    const auto& a = points_[hi - 1];
    const auto& b = points_[hi];

    // Widen before subtracting: callers may supply the full int32 range.
    const double t = (static_cast<double>(raw) - a.raw) / (static_cast<double>(b.raw) - a.raw);
    return static_cast<float>(a.value + t * (static_cast<double>(b.value) - a.value));
  }

  [[nodiscard]] constexpr bool covers(std::int32_t raw) const noexcept {
    return !points_.empty() && raw >= points_.front().raw && raw <= points_.back().raw;
  }

  [[nodiscard]] constexpr const Points& points() const noexcept { return points_; }
  [[nodiscard]] constexpr std::size_t size() const noexcept { return points_.size(); }

 private:
  Points points_;
};

}  // namespace c3::utils
