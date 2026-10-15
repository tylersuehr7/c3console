// First-order IIR low-pass with a time constant, not a fixed alpha, so the response does not
// change if the sample cadence does. Time constants differ per channel by design (AGENTS.md):
// fuel 10 s (it sloshes), coolant 2 s, oil pressure 0.2 s so a pressure drop is seen.
#pragma once

namespace c3fw {

class Iir {
public:
  explicit constexpr Iir(float tau_s) noexcept : tau_s_(tau_s) {}

  /// Feeds one sample taken dt_s after the previous one; the first sample primes the state.
  constexpr float update(float x, float dt_s) noexcept {
    if (!primed_) {
      primed_ = true;
      y_ = x;
      return y_;
    }
    const float alpha = dt_s / (tau_s_ + dt_s);
    y_ += alpha * (x - y_);
    return y_;
  }

  constexpr void reset() noexcept { primed_ = false; }
  [[nodiscard]] constexpr float value() const noexcept { return y_; }
  [[nodiscard]] constexpr bool primed() const noexcept { return primed_; }
  [[nodiscard]] constexpr float tau_s() const noexcept { return tau_s_; }

private:
  float tau_s_;
  float y_ = 0.0F;
  bool primed_ = false;
};

// Spec §3.4 time constants, seconds.
constexpr float kFuelTauS = 10.0F;
constexpr float kCoolantTauS = 2.0F;
constexpr float kOilTauS = 0.2F;
constexpr float kVoltTauS = 0.5F;  // not in the spec; charging dips should still be visible

}  // namespace c3fw
