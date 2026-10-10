#include "mock_source.hpp"

#include "limits.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace c3::telemetry {

namespace {

/// Drive profile parameters, verbatim from the reference PROFILES table.
struct DriveParams {
  double rpm_lo;
  double rpm_hi;
  double speed_mph;
  double clt_target_f;
  double fuel_drain;  // tank fraction per second; see step()
};

constexpr DriveParams kColdStart{820, 820, 0, 70, 0.00002};
constexpr DriveParams kWarmupDrive{1900, 3100, 45, 196, 0.00028};
constexpr DriveParams kHotIdle{760, 820, 0, 214, 0.00009};
constexpr DriveParams kHighwayCruise{2600, 2900, 70, 198, 0.00034};

constexpr const DriveParams& drive_for(MockProfile p) noexcept {
  switch (p) {
    case MockProfile::ColdStart:
      return kColdStart;
    case MockProfile::HotIdle:
      return kHotIdle;
    case MockProfile::HighwayCruise:
      return kHighwayCruise;
    case MockProfile::WarmupDrive:
    case MockProfile::OilPressureLoss:
    case MockProfile::Overheat:
    case MockProfile::SenderOpen:
    case MockProfile::AlternatorFail:
    case MockProfile::LinkLoss:
    case MockProfile::Count:
      return kWarmupDrive;
  }
  return kWarmupDrive;
}

// ---------------------------------------------------------------------------
// Simulated MCU. In the car these thresholds live in firmware/esp32 and arrive in the
// frame; the Pi never evaluates them. Kept together so nobody mistakes them for UI logic.
// ---------------------------------------------------------------------------
namespace mock_mcu {

// Synthetic raw counts, the inverse of the placeholder identity table in
// config/calibration.json so the calibration screen round-trips under mock. That table maps
// raw 0..32767 linearly onto each channel's plausibility limits, so the mock does the same.
constexpr double kFullScaleCounts = 32767.0;

constexpr std::int32_t raw_counts(double value, float lo, float hi) noexcept {
  const double fraction = (value - lo) / (static_cast<double>(hi) - lo);
  return static_cast<std::int32_t>(std::clamp(fraction, 0.0, 1.0) * kFullScaleCounts);
}

}  // namespace mock_mcu

// First-order approach: moves `state` toward `target` by rate*dt, capped at one full step.
constexpr void approach(double& state, double target, double rate, double dt) noexcept {
  state += (target - state) * std::min(1.0, rate * dt);
}

}  // namespace

std::optional<MockProfile> parse_mock_profile(std::string_view text) noexcept {
  std::uint8_t i;
  for (i = 0; i < static_cast<std::uint8_t>(MockProfile::Count); i++) {
    const auto profile = static_cast<MockProfile>(i);
    if (text == to_string(profile)) {
      return profile;
    }
  }
  return std::nullopt;
}

MockSource::MockSource(MockProfile profile, utils::MonotonicClock clock) noexcept : profile_(profile), clock_(clock) {}

void MockSource::step(double dt_s) noexcept {
  const DriveParams& p = drive_for(profile_);
  t_s_ += dt_s;

  // rpm wanders inside the profile band
  const double band = p.rpm_hi - p.rpm_lo;
  const double wobble = 0.5 + 0.5 * std::sin(t_s_ * 0.7) * (0.6 + 0.4 * std::sin(t_s_ * 0.23));
  approach(rpm_, p.rpm_lo + band * wobble, 2.2, dt_s);

  // coolant: first-order approach to setpoint; road speed helps it cool
  double clt_target = p.clt_target_f - p.speed_mph * 0.06;
  if (profile_ == MockProfile::Overheat) {
    clt_target = 251.0;
  }
  approach(coolant_f_, clt_target, 0.055, dt_s);

  // oil pressure: f(rpm, temp). High when cold, ~40 hot at cruise, ~15 hot at idle.
  const double viscosity = 1.0 + std::max(0.0, 190.0 - coolant_f_) / 95.0;
  double oil = (10.0 + rpm_ * 0.0122) * viscosity;
  if (profile_ == MockProfile::OilPressureLoss) {
    oil *= std::max(0.06, 1.0 - std::fmod(t_s_, 22.0) / 16.0);
  }
  approach(oil_psi_, std::min(78.0, oil), 3.2, dt_s);

  // fuel: monotonic drain. Deviation from the reference: stepModel() multiplied by 60
  // because it drained per 60 Hz frame, emptying a tank in ~40 s for the browser demo.
  // Here the rate is per second, so WarmupDrive empties a tank in about an hour: fast
  // enough to watch, plausible enough not to trip FUEL_LOW during a normal test drive.
  fuel_fraction_ = std::max(0.0, fuel_fraction_ - p.fuel_drain * dt_s);

  // volts: charging, sagging under blower, compressor and lamp load
  const double load = static_cast<std::uint8_t>(loads_.blower) * 0.11 + (loads_.ac ? 0.19 : 0.0) +
                      (loads_.headlights ? 0.17 : 0.0) + 0.14;
  const double volt_target = profile_ == MockProfile::AlternatorFail ? 11.6 - load * 0.5 : 14.35 - load * 0.16;
  approach(voltage_v_, volt_target, 1.9, dt_s);
}

void MockFaults::apply(Frame& frame, bool oil_sender_open) noexcept {
  const std::array<bool, 5> set = {frame.oil_psi < 12.0F, frame.coolant_f > 235.0F, frame.fuel_fraction < 0.08F,
                                   frame.voltage_v < 12.4F, frame.voltage_v > 15.2F};

  const std::array<bool, 5> clear = {frame.oil_psi >= 15.0F, frame.coolant_f <= 228.0F, frame.fuel_fraction >= 0.10F,
                                     frame.voltage_v >= 12.8F, frame.voltage_v <= 14.9F};

  constexpr std::array<FaultKind, 5> kKinds = {FaultKind::OilLow, FaultKind::CltHigh, FaultKind::FuelLow,
                                               FaultKind::VoltLow, FaultKind::VoltHigh};

  frame.faults.clear();

  for (std::size_t i = 0; i < active_.size(); ++i) {
    active_[i] = set[i] || (active_[i] && !clear[i]);

    if (i == 0 && oil_sender_open) {
      active_[i] = false;
    }

    if (active_[i]) {
      static_cast<void>(frame.faults.push_back({.kind = kKinds[i], .channel = {}}));
    }
  }

  if (oil_sender_open) {
    static_cast<void>(frame.faults.push_back({.kind = FaultKind::SenderOpen, .channel = Channel::Oil}));
  }
}

Frame MockSource::make_frame() noexcept {
  const bool oil_sender_open = profile_ == MockProfile::SenderOpen;

  Frame f;
  f.t_ms = static_cast<std::uint32_t>(ticks_ * kPeriodMs);
  f.fuel_fraction = static_cast<float>(fuel_fraction_);
  f.oil_psi = oil_sender_open ? 0.0F : static_cast<float>(oil_psi_);
  f.coolant_f = static_cast<float>(coolant_f_);
  f.voltage_v = static_cast<float>(voltage_v_);

  using mock_mcu::raw_counts;
  f.raw[static_cast<std::size_t>(Channel::Fuel)] = raw_counts(fuel_fraction_, limits::kFuelMin, limits::kFuelMax);
  f.raw[static_cast<std::size_t>(Channel::Oil)] =
      oil_sender_open ? 0 : raw_counts(oil_psi_, limits::kOilMinPsi, limits::kOilMaxPsi);
  f.raw[static_cast<std::size_t>(Channel::Coolant)] = raw_counts(coolant_f_, limits::kCltMinF, limits::kCltMaxF);
  f.raw[static_cast<std::size_t>(Channel::Volt)] = raw_counts(voltage_v_, limits::kVoltMin, limits::kVoltMax);

  faults_.apply(f, oil_sender_open);

  f.relays = vehicle::RelayState{.blower = loads_.blower,
                                 .mode = loads_.mode,
                                 .ac_enabled = loads_.ac,
                                 .headlights_on = loads_.headlights,
                                 .lamp_doors_open = loads_.doors,
                                 .highbeams_on = loads_.high};

  return f;
}

std::optional<Frame> MockSource::poll() noexcept {
  const std::uint64_t now = clock_();
  if (!started_ || now < start_ms_) {  // second clause: a clock that stepped backwards
    started_ = true;
    start_ms_ = now;
    ticks_ = 0;
  }

  const std::uint64_t due = (now - start_ms_) / kPeriodMs + 1;  // first poll emits tick 1
  if (due <= ticks_) {
    return std::nullopt;
  }

  // Catch up in fixed steps so the model is independent of poll cadence.
  std::uint64_t pending = due - ticks_;
  if (pending > kMaxCatchUpTicks) {
    ticks_ += pending - kMaxCatchUpTicks;  // drop the gap; never simulate a long pause
    pending = kMaxCatchUpTicks;
  }

  Frame latest;
  for (std::uint64_t i = 0; i < pending; ++i) {
    step(kPeriodMs / 1000.0);
    ++ticks_;
    latest = make_frame();
  }

  if (profile_ == MockProfile::LinkLoss && t_s_ > kLinkLossAfterS) {
    // The model keeps running, as the MCU would; the Pi just stops hearing it.
    health_.state = LinkState::Stalled;
    return std::nullopt;
  }

  health_.state = LinkState::Live;
  health_.last_frame_monotonic_ms = now;
  ++health_.frames_ok;
  return latest;
}

SourceHealth MockSource::health() const noexcept {
  return health_;
}

}  // namespace c3::telemetry
