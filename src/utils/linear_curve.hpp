// A fixed-capacity piecewise-linear lookup table: raw sender reading in, engineering value out.
//
// Bench sweeps rarely reach the physical limits of a sender, so the end segments extrapolate
// instead of clamping. covers() tells callers when a reading relies on that extrapolation.
#pragma once

#include "fixed_vector.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace c3::utils {

/// One calibration knot: a raw sender reading and the engineering value it maps to.
struct CurvePoint {
  std::int32_t raw = 0;  // sender reading in the MCU's native units
  float value = 0.0F;    // engineering value at that reading
};

/// Piecewise-linear calibration curve with at most kCapacity knots, sorted by raw.
class LinearCurve {
public:
  /// Maximum number of knots; enough for a dense bench sweep without heap allocation.
  static constexpr std::size_t kCapacity = 24;

  /// Knot storage, kept sorted by raw ascending.
  using Points = FixedVector<CurvePoint, kCapacity>;

  /// Creates an empty curve; interpolate() returns 0 and covers() nothing until knots are added.
  constexpr LinearCurve() noexcept = default;

  /// Creates a straight line through two knots.
  ///
  /// Not validated: `a.raw` must be less than `b.raw`. Use from_points() for data that did not
  /// come from a literal.
  ///
  /// @param a the lower knot
  /// @param b the upper knot
  constexpr LinearCurve(CurvePoint a, CurvePoint b) noexcept {
    static_cast<void>(points_.push_back(a));
    static_cast<void>(points_.push_back(b));
  }

  /// Builds a curve from a table, such as one loaded from config/calibration.json.
  ///
  /// Requirements:
  /// - At least two knots
  /// - raw strictly increasing
  /// - Every value finite (no NaN or infinity)
  ///
  /// @param points the knots, already sorted by raw
  /// @return the curve if every requirement holds; otherwise nullopt
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

  /// Inserts a knot, keeping the table sorted by raw.
  ///
  /// A knot whose raw already exists replaces that entry in place, so replacement still works
  /// when the table is full.
  ///
  /// @param point the knot to insert or replace
  /// @return true if the knot was stored; otherwise false (non-finite value, or full and raw is new)
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

  /// Maps a raw reading to a value along the segment that contains it.
  ///
  /// Readings below the first knot or above the last extend the end segment: extrapolation, not
  /// clamping. Check covers() when that distinction matters.
  ///
  /// @param raw the sender reading
  /// @return the interpolated value, or 0 when the curve holds fewer than two knots
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

  /// Reports whether a reading falls inside the measured range, first knot to last inclusive.
  ///
  /// @param raw the sender reading
  /// @return true if the reading is covered by measured data; otherwise false (empty curve, or
  ///         interpolate() would extrapolate)
  [[nodiscard]] constexpr bool covers(std::int32_t raw) const noexcept {
    return !points_.empty() && raw >= points_.front().raw && raw <= points_.back().raw;
  }

  /// @return the knots, sorted by raw ascending
  [[nodiscard]] constexpr const Points& points() const noexcept { return points_; }

  /// @return the number of knots in the table
  [[nodiscard]] constexpr std::size_t size() const noexcept { return points_.size(); }

private:
  Points points_;
};

}  // namespace c3::utils
