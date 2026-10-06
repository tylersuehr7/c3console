// Calibration table — raw ADC counts to engineering units, per channel.
// Loaded once at startup from config/calibration.json and never modified on the hot path.
#pragma once

#include "frame.hpp"
#include "utils/linear_curve.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>

namespace c3::telemetry {

using CalibrationPoint = utils::CurvePoint;
using CalibrationCurve = utils::LinearCurve;

constexpr std::size_t kMinMeasuredPoints = 8;
constexpr std::size_t kMaxCalibrationPoints = CalibrationCurve::kCapacity;

struct CalibrationTable {
  bool placeholder = true;  // never cleared by code; only by a file that passes the guard
  std::array<CalibrationCurve, kChannelCount> curves{};

  [[nodiscard]] const CalibrationCurve& curve(Channel ch) const noexcept {
    return curves[static_cast<std::size_t>(ch)];
  }

  [[nodiscard]] float convert(Channel ch, std::int32_t raw) const noexcept { return curve(ch).interpolate(raw); }
};

struct CalibrationLoadResult {
  std::optional<CalibrationTable> table;  // nullopt when `error` is set
  std::string error;
};

[[nodiscard]] CalibrationLoadResult parse_calibration(std::string_view json_text) noexcept;
[[nodiscard]] CalibrationLoadResult load_calibration(const std::string& path) noexcept;
[[nodiscard]] std::string describe(const CalibrationTable& table) noexcept;

}  // namespace c3::telemetry
