// Plausibility limits for values arriving from outside the process: the UART frame and the replay CSV.
// A value beyond these is corrupt data, rejected before it reaches a gauge. They are deliberately far
// wider than any warning threshold; the MCU decides faults (invariant 1).

#pragma once

namespace c3::telemetry::limits {

constexpr float kFuelMin = 0.0F;
constexpr float kFuelMax = 1.0F;
constexpr float kOilMinPsi = 0.0F;
constexpr float kOilMaxPsi = 200.0F;
constexpr float kCltMinF = -40.0F;
constexpr float kCltMaxF = 400.0F;
constexpr float kVoltMin = 0.0F;
constexpr float kVoltMax = 40.0F;

}  // namespace c3::telemetry::limits
