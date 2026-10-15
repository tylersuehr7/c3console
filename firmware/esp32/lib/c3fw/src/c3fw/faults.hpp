// Fault evaluation with hysteresis (spec §4.1 codes; AGENTS.md safety invariant 1).
//
// This is the one place in the whole system that turns a sensor value into a warning. The
// thresholds are the spec §4.3 table, which the desktop MockSource also simulates so the UI
// behaves the same against both; tests/test_firmware_faults.cpp pins the numbers. Each value
// fault sets at one level and clears at a safer one so a reading that hovers on the line does
// not chatter the buzzer. A channel with a sender fault contributes no value fault: its value
// is meaningless.
//
// The warning path (LED + buzzer) is derived here and driven from GPIO by the firmware
// regardless of UART state (invariant 2). It fails toward warning: a sender or bus fault on
// oil or coolant lights the LED, because the path must not go dark exactly when it can no
// longer measure, and an unreadable voltage channel does not mute the oil buzzer.
#pragma once

#include "c3fw/sender_check.hpp"
#include "telemetry/frame.hpp"
#include "utils/fixed_vector.hpp"

#include <array>
#include <cstdint>

namespace c3fw {

/// Spec §4.3 set/clear levels. Defaults are the shipped values; tests construct variants.
struct Thresholds {
  float oil_low_set_psi = 12.0F;
  float oil_low_clear_psi = 15.0F;
  float clt_high_set_f = 235.0F;
  float clt_high_clear_f = 228.0F;
  float fuel_low_set = 0.08F;
  float fuel_low_clear = 0.10F;
  float volt_low_set = 12.4F;
  float volt_low_clear = 12.8F;
  float volt_high_set = 15.2F;
  float volt_high_clear = 14.9F;
  // The buzzer for low oil is gated on the alternator charging, i.e. the engine running:
  // 0 psi at key-on-engine-off is normal and must not scream. The LED is never gated.
  float engine_running_volts = 13.2F;
};

/// One tick's converted values plus the sender windows' verdicts, in Channel order.
struct Reading {
  float fuel_fraction = 0.0F;  // fraction
  float oil_psi = 0.0F;
  float coolant_f = 0.0F;
  float voltage_v = 0.0F;
  std::array<SenderState, c3::telemetry::kChannelCount> sender{};
};

using FaultList = c3::utils::FixedVector<c3::telemetry::Fault, c3::telemetry::kMaxFaults>;

/// What the GPIO warning outputs should show.
struct Warning {
  bool led = false;
  bool buzzer = false;
};

class FaultEvaluator {
public:
  constexpr FaultEvaluator() noexcept = default;
  explicit constexpr FaultEvaluator(const Thresholds& t) noexcept : t_(t) {}

  /// Re-evaluates every fault against `r` and returns the list in spec order: the value
  /// faults, then SENDER_OPEN / SENDER_SHORT per channel.
  constexpr FaultList evaluate(const Reading& r) noexcept {
    using c3::telemetry::Channel;
    using c3::telemetry::Fault;
    using c3::telemetry::FaultKind;

    const bool fuel_ok = r.sender[idx(Channel::Fuel)] == SenderState::Ok;
    const bool oil_ok = r.sender[idx(Channel::Oil)] == SenderState::Ok;
    const bool clt_ok = r.sender[idx(Channel::Coolant)] == SenderState::Ok;
    const bool volt_ok = r.sender[idx(Channel::Volt)] == SenderState::Ok;

    oil_low_ = oil_ok && latch(oil_low_, r.oil_psi < t_.oil_low_set_psi, r.oil_psi >= t_.oil_low_clear_psi);
    clt_high_ = clt_ok && latch(clt_high_, r.coolant_f > t_.clt_high_set_f, r.coolant_f <= t_.clt_high_clear_f);
    fuel_low_ = fuel_ok && latch(fuel_low_, r.fuel_fraction < t_.fuel_low_set, r.fuel_fraction >= t_.fuel_low_clear);
    volt_low_ = volt_ok && latch(volt_low_, r.voltage_v < t_.volt_low_set, r.voltage_v >= t_.volt_low_clear);
    volt_high_ = volt_ok && latch(volt_high_, r.voltage_v > t_.volt_high_set, r.voltage_v <= t_.volt_high_clear);

    // The channels the warning path depends on. Unreadable counts as faulted, so the LED
    // comes on and the oil buzzer is not silenced by a volt channel that cannot be trusted.
    critical_sender_fault_ = !oil_ok || !clt_ok;
    engine_running_ = !volt_ok || r.voltage_v >= t_.engine_running_volts;

    FaultList out;
    const auto add = [&](FaultKind kind, Channel ch = Channel::Fuel) noexcept {
      static_cast<void>(out.push_back(Fault{.kind = kind, .channel = ch}));
    };
    if (oil_low_) {
      add(FaultKind::OilLow);
    }
    if (clt_high_) {
      add(FaultKind::CltHigh);
    }
    if (fuel_low_) {
      add(FaultKind::FuelLow);
    }
    if (volt_low_) {
      add(FaultKind::VoltLow);
    }
    if (volt_high_) {
      add(FaultKind::VoltHigh);
    }
    for (std::size_t i = 0; i < c3::telemetry::kChannelCount; ++i) {
      if (r.sender[i] == SenderState::Open) {
        add(FaultKind::SenderOpen, static_cast<Channel>(i));
      } else if (r.sender[i] == SenderState::Short) {
        add(FaultKind::SenderShort, static_cast<Channel>(i));
      }
    }
    return out;
  }

  /// LED and buzzer for the last evaluation. The buzzer sounds for CLT_HIGH unconditionally
  /// and for OIL_LOW while the engine is running; the LED also lights when the oil or
  /// coolant sender cannot be read.
  [[nodiscard]] constexpr Warning warning() const noexcept {
    return {.led = oil_low_ || clt_high_ || critical_sender_fault_,
            .buzzer = clt_high_ || (oil_low_ && engine_running_)};
  }

  [[nodiscard]] constexpr bool oil_low() const noexcept { return oil_low_; }
  [[nodiscard]] constexpr bool clt_high() const noexcept { return clt_high_; }
  [[nodiscard]] constexpr bool engine_running() const noexcept { return engine_running_; }
  [[nodiscard]] constexpr const Thresholds& thresholds() const noexcept { return t_; }

private:
  [[nodiscard]] static constexpr std::size_t idx(c3::telemetry::Channel ch) noexcept {
    return static_cast<std::size_t>(ch);
  }
  // Set/reset latch with a dead band between the two conditions.
  [[nodiscard]] static constexpr bool latch(bool current, bool set, bool clear) noexcept {
    if (set) {
      return true;
    }
    if (clear) {
      return false;
    }
    return current;
  }

  Thresholds t_{};
  bool oil_low_ = false;
  bool clt_high_ = false;
  bool fuel_low_ = false;
  bool volt_low_ = false;
  bool volt_high_ = false;
  bool engine_running_ = false;
  bool critical_sender_fault_ = false;
};

}  // namespace c3fw
