// The MCU's signal chain in isolation: median-of-5, the per-channel IIR, the sender windows,
// the relay output map, calibration staging, and the shipped placeholder curves against the
// JSON they are generated from.
#include "c3fw/calibration.hpp"
#include "c3fw/filter.hpp"
#include "c3fw/lookup.hpp"
#include "c3fw/median.hpp"
#include "c3fw/relays.hpp"
#include "c3fw/sender_check.hpp"
#include "telemetry/calibration.hpp"
#include "telemetry/frame.hpp"
#include "utils/linear_curve.hpp"
#include "vehicle/vehicle_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

using c3::telemetry::Channel;
using c3::telemetry::kChannelCount;
using c3::vehicle::HvacBlower;
using c3::vehicle::HvacMode;
using c3fw::Calibration;
using c3fw::CalPoint;
using c3fw::check_sender;
using c3fw::CommandedState;
using c3fw::Curve;
using c3fw::default_sender_windows;
using c3fw::Iir;
using c3fw::median5;
using c3fw::Output;
using c3fw::Outputs;
using c3fw::outputs_for;
using c3fw::SenderState;
using c3fw::SenderWindow;
using Catch::Matchers::WithinAbs;

CATCH_REGISTER_ENUM(SenderState, SenderState::Ok, SenderState::Open, SenderState::Short)

namespace {

constexpr std::size_t idx(Channel ch) noexcept {
  return static_cast<std::size_t>(ch);
}
constexpr std::size_t idx(Output o) noexcept {
  return static_cast<std::size_t>(o);
}

/// Which blower taps are energised, as a count and a position.
struct Taps {
  std::size_t count = 0;
  std::size_t which = 0;
};
[[nodiscard]] constexpr Taps taps(const Outputs& out) noexcept {
  Taps t;
  for (std::size_t i = 0; i < c3fw::kBlowerTapCount; ++i) {
    if (out[i]) {
      ++t.count;
      t.which = i;
    }
  }
  return t;
}

/// A monotonic staging table of `n` points starting at raw 0.
[[nodiscard]] Curve staged(std::size_t n) {
  Curve c;
  for (std::size_t i = 0; i < n; ++i) {
    REQUIRE(c.add({static_cast<std::int32_t>(i * 1000), static_cast<float>(i)}));
  }
  return c;
}

}  // namespace

// ---------------------------------------------------------------------------
// median5
// ---------------------------------------------------------------------------

TEST_CASE("median5: picks the middle value and so rejects a single spike in either direction", "[firmware][filter]") {
  STATIC_CHECK(median5({1, 2, 3, 4, 5}) == 3);
  STATIC_CHECK(median5({5, 4, 3, 2, 1}) == 3);
  STATIC_CHECK(median5({100, 101, 32767, 99, 100}) == 100);   // ignition spike up
  STATIC_CHECK(median5({100, 101, -32768, 99, 100}) == 100);  // and down
  STATIC_CHECK(median5({-5, -1, -3, -2, -4}) == -3);          // differential inputs go negative
  STATIC_CHECK(median5({7, 7, 7, 7, 7}) == 7);
  STATIC_CHECK(median5({0, 0, 1, 1, 1}) == 1);
}

// ---------------------------------------------------------------------------
// Iir
// ---------------------------------------------------------------------------

TEST_CASE("Iir: the first sample primes the state instead of decaying from zero", "[firmware][filter]") {
  Iir f(10.0F);
  CHECK_FALSE(f.primed());
  CHECK(f.update(12000.0F, 0.1F) == 12000.0F);
  CHECK(f.primed());
  CHECK(f.value() == 12000.0F);
  f.reset();
  CHECK_FALSE(f.primed());
  CHECK(f.update(3.0F, 0.1F) == 3.0F);
}

TEST_CASE("Iir: a unit step reaches the discrete-time fraction for its time constant", "[firmware][filter]") {
  // alpha = dt / (tau + dt); after n steps y = 1 - (1 - alpha)^n. tau 1 s, dt 0.1 s, n 10.
  Iir f(1.0F);
  static_cast<void>(f.update(0.0F, 0.1F));
  float y = 0.0F;
  for (int i = 0; i < 10; ++i) {
    y = f.update(1.0F, 0.1F);
  }
  const double alpha = 0.1 / 1.1;
  CHECK_THAT(y, WithinAbs(1.0 - std::pow(1.0 - alpha, 10), 1e-5));
  CHECK(f.tau_s() == 1.0F);
}

TEST_CASE("Iir: the response depends on the time constant, not the sample cadence", "[firmware][filter]") {
  // Ten 0.1 s steps and one 1.0 s step cover the same time; the one-step filter lands at the
  // continuous value 1 - e^-1 for tau 1 s, the ten-step one close to it.
  Iir ten(1.0F);
  Iir one(1.0F);
  static_cast<void>(ten.update(0.0F, 0.1F));
  static_cast<void>(one.update(0.0F, 1.0F));
  float y_ten = 0.0F;
  for (int i = 0; i < 10; ++i) {
    y_ten = ten.update(1.0F, 0.1F);
  }
  const float y_one = one.update(1.0F, 1.0F);
  CHECK_THAT(y_one, WithinAbs(0.5, 1e-6));  // alpha = 1/(1+1)
  CHECK_THAT(y_ten, WithinAbs(0.614, 0.01));
  CHECK(y_ten > y_one);  // finer steps approach the continuous 0.632
}

TEST_CASE("Iir: the per-channel time constants are the AGENTS.md ones and are not unified", "[firmware][filter]") {
  STATIC_CHECK(c3fw::kFuelTauS == 10.0F);
  STATIC_CHECK(c3fw::kCoolantTauS == 2.0F);
  STATIC_CHECK(c3fw::kOilTauS == 0.2F);
  STATIC_CHECK(c3fw::kOilTauS < c3fw::kCoolantTauS);
  STATIC_CHECK(c3fw::kCoolantTauS < c3fw::kFuelTauS);
  STATIC_CHECK(c3fw::kVoltTauS < c3fw::kCoolantTauS);

  // One 100 ms tick of a pressure drop is visible on oil and nearly invisible on fuel.
  constexpr auto one_tick = [](float tau) {
    Iir f(tau);
    static_cast<void>(f.update(1.0F, 0.1F));
    return f.update(0.0F, 0.1F);
  };
  STATIC_CHECK(one_tick(c3fw::kOilTauS) < 0.7F);
  STATIC_CHECK(one_tick(c3fw::kCoolantTauS) > 0.9F);
  STATIC_CHECK(one_tick(c3fw::kFuelTauS) > 0.99F);
}

// ---------------------------------------------------------------------------
// check_sender
// ---------------------------------------------------------------------------

TEST_CASE("check_sender: open is strictly above 97 % of full scale, short strictly below 1 %", "[firmware][filter]") {
  constexpr SenderWindow both{.detect_open = true, .detect_short = true};
  STATIC_CHECK(both.open_above == 31784);  // 0.97 x 32767
  STATIC_CHECK(both.short_below == 327);   // 0.01 x 32767
  STATIC_CHECK(check_sender(31784, both) == SenderState::Ok);
  STATIC_CHECK(check_sender(31785, both) == SenderState::Open);
  STATIC_CHECK(check_sender(c3fw::kFullScaleCounts, both) == SenderState::Open);
  STATIC_CHECK(check_sender(327, both) == SenderState::Ok);
  STATIC_CHECK(check_sender(326, both) == SenderState::Short);
  STATIC_CHECK(check_sender(0, both) == SenderState::Short);
  STATIC_CHECK(check_sender(-32768, both) == SenderState::Short);
  STATIC_CHECK(check_sender(16000, both) == SenderState::Ok);
}

TEST_CASE("check_sender: a disabled window never fires", "[firmware][filter]") {
  constexpr SenderWindow none{.detect_open = false, .detect_short = false};
  STATIC_CHECK(check_sender(32767, none) == SenderState::Ok);
  STATIC_CHECK(check_sender(-32768, none) == SenderState::Ok);
  constexpr SenderWindow open_only{.detect_open = true, .detect_short = false};
  STATIC_CHECK(check_sender(0, open_only) == SenderState::Ok);
  STATIC_CHECK(check_sender(32767, open_only) == SenderState::Open);
}

TEST_CASE("default_sender_windows: open on fuel, oil and coolant; short only on coolant; nothing on volt",
          "[firmware][filter]") {
  constexpr auto w = default_sender_windows();
  STATIC_CHECK(check_sender(32767, w[idx(Channel::Fuel)]) == SenderState::Open);
  STATIC_CHECK(check_sender(0, w[idx(Channel::Fuel)]) == SenderState::Ok);  // 0 ohm is empty, not a short
  STATIC_CHECK(check_sender(32767, w[idx(Channel::Oil)]) == SenderState::Open);
  STATIC_CHECK(check_sender(0, w[idx(Channel::Oil)]) == SenderState::Ok);  // ~1 ohm is 0 psi; OIL_LOW covers it
  STATIC_CHECK(check_sender(32767, w[idx(Channel::Coolant)]) == SenderState::Open);
  STATIC_CHECK(check_sender(0, w[idx(Channel::Coolant)]) == SenderState::Short);
  STATIC_CHECK(check_sender(32767, w[idx(Channel::Volt)]) == SenderState::Ok);
  STATIC_CHECK(check_sender(0, w[idx(Channel::Volt)]) == SenderState::Ok);
}

// ---------------------------------------------------------------------------
// outputs_for
// ---------------------------------------------------------------------------

TEST_CASE("outputs_for: the default state energises nothing", "[firmware][relays]") {
  constexpr Outputs out = outputs_for(CommandedState{});
  STATIC_CHECK(out == Outputs{});
}

TEST_CASE("outputs_for: each blower speed energises exactly its own tap and speed 0 none", "[firmware][relays]") {
  CommandedState s;
  for (const HvacBlower level : {HvacBlower::L1, HvacBlower::L2, HvacBlower::L3, HvacBlower::L4}) {
    s.relays.blower = level;
    const Taps t = taps(outputs_for(s));
    CHECK(t.count == 1);
    CHECK(t.which == static_cast<std::size_t>(level) - 1);
  }
  s.relays.blower = HvacBlower::Off;
  CHECK(taps(outputs_for(s)).count == 0);
}

TEST_CASE("outputs_for: the compressor clutch needs airflow even if the state says otherwise", "[firmware][relays]") {
  CommandedState s;
  s.relays.ac_enabled = true;
  s.relays.blower = HvacBlower::Off;
  CHECK_FALSE(outputs_for(s)[idx(Output::AcClutch)]);
  s.relays.blower = HvacBlower::L1;
  CHECK(outputs_for(s)[idx(Output::AcClutch)]);
  s.relays.ac_enabled = false;
  CHECK_FALSE(outputs_for(s)[idx(Output::AcClutch)]);
}

TEST_CASE("outputs_for: vent releases both solenoids, floor and defrost energise one each", "[firmware][relays]") {
  CommandedState s;
  s.relays.mode = HvacMode::Vent;
  CHECK_FALSE(outputs_for(s)[idx(Output::FloorSolenoid)]);
  CHECK_FALSE(outputs_for(s)[idx(Output::DefrostSolenoid)]);
  s.relays.mode = HvacMode::Floor;
  CHECK(outputs_for(s)[idx(Output::FloorSolenoid)]);
  CHECK_FALSE(outputs_for(s)[idx(Output::DefrostSolenoid)]);
  s.relays.mode = HvacMode::Defrost;
  CHECK_FALSE(outputs_for(s)[idx(Output::FloorSolenoid)]);
  CHECK(outputs_for(s)[idx(Output::DefrostSolenoid)]);
}

TEST_CASE("outputs_for: lamp, high-beam and door relays follow their commanded bits independently",
          "[firmware][relays]") {
  CommandedState s;
  s.relays.headlights_on = true;
  CHECK(outputs_for(s)[idx(Output::HeadlampRelay)]);
  CHECK_FALSE(outputs_for(s)[idx(Output::HighBeamRelay)]);
  CHECK_FALSE(outputs_for(s)[idx(Output::DoorRelay)]);
  s.high = true;
  s.doors = true;
  const Outputs out = outputs_for(s);
  CHECK(out[idx(Output::HeadlampRelay)]);
  CHECK(out[idx(Output::HighBeamRelay)]);
  CHECK(out[idx(Output::DoorRelay)]);
  // HVAC outputs are untouched by lamp state.
  CHECK(taps(out).count == 0);
  CHECK_FALSE(out[idx(Output::AcClutch)]);
}

TEST_CASE("Output: the blower taps lead the table so break-before-make can address them by index",
          "[firmware][relays]") {
  STATIC_CHECK(idx(Output::Blower1) == 0);
  STATIC_CHECK(idx(Output::Blower4) == c3fw::kBlowerTapCount - 1);
  STATIC_CHECK(idx(Output::AcClutch) == c3fw::kBlowerTapCount);
  STATIC_CHECK(c3fw::kOutputCount == 10);
}

// ---------------------------------------------------------------------------
// Calibration
// ---------------------------------------------------------------------------

TEST_CASE("Calibration: boots on the placeholder curves with no channel marked measured", "[firmware][calibration]") {
  const Calibration cal;
  const auto placeholders = c3fw::placeholder_curves();
  for (std::size_t i = 0; i < kChannelCount; ++i) {
    const auto ch = static_cast<Channel>(i);
    CHECK_FALSE(cal.measured(ch));
    CHECK(cal.staged(ch) == nullptr);
    CHECK(cal.active(ch).interpolate(16384) == placeholders[i].interpolate(16384));
  }
}

TEST_CASE("Calibration: cal add stages points without touching the active curve", "[firmware][calibration]") {
  Calibration cal;
  const float before = cal.active(Channel::Oil).interpolate(16384);
  for (std::size_t i = 0; i < Calibration::kMinimumPoints - 1; ++i) {
    CHECK(cal.add(Channel::Oil, {static_cast<std::int32_t>(i * 1000), static_cast<float>(i) * 10.0F}));
    CHECK(cal.staged(Channel::Oil) == nullptr);  // not enough yet
  }
  CHECK(cal.active(Channel::Oil).interpolate(16384) == before);
  CHECK_FALSE(cal.measured(Channel::Oil));

  CHECK(cal.add(Channel::Oil, {7000, 70.0F}));
  REQUIRE(cal.staged(Channel::Oil) != nullptr);
  CHECK(cal.staged(Channel::Oil)->size() == Calibration::kMinimumPoints);
  CHECK(cal.active(Channel::Oil).interpolate(16384) == before);  // still the placeholder
}

TEST_CASE("Calibration: a point at an existing raw replaces it; a non-finite value or a full table is refused",
          "[firmware][calibration]") {
  Calibration cal;
  CHECK(cal.add(Channel::Fuel, {100, 0.1F}));
  CHECK(cal.add(Channel::Fuel, {100, 0.2F}));  // replaced in place
  CHECK_FALSE(cal.add(Channel::Fuel, {200, std::numeric_limits<float>::quiet_NaN()}));
  for (std::size_t i = 1; i < c3fw::kMaxCalPoints; ++i) {
    CHECK(cal.add(Channel::Fuel, {static_cast<std::int32_t>(1000 * i), static_cast<float>(i)}));
  }
  CHECK_FALSE(cal.add(Channel::Fuel, {999999, 1.0F}));  // full
  CHECK(cal.add(Channel::Fuel, {1000, 5.0F}));          // replacement still works when full
}

TEST_CASE("Calibration: activate needs eight valid points, then marks the channel measured and clears staging",
          "[firmware][calibration]") {
  Calibration cal;
  CHECK_FALSE(cal.activate(Channel::Coolant, Curve{}));
  CHECK_FALSE(cal.activate(Channel::Coolant, staged(Calibration::kMinimumPoints - 1)));
  CHECK_FALSE(cal.measured(Channel::Coolant));

  for (std::size_t i = 0; i < Calibration::kMinimumPoints; ++i) {
    REQUIRE(cal.add(Channel::Coolant, {static_cast<std::int32_t>(i * 1000), static_cast<float>(i)}));
  }
  const Curve* ready = cal.staged(Channel::Coolant);
  REQUIRE(ready != nullptr);
  const Curve snapshot = ready != nullptr ? *ready : Curve{};  // what main.cpp hands to the NVS worker
  CHECK(cal.activate(Channel::Coolant, snapshot));
  CHECK(cal.measured(Channel::Coolant));
  CHECK_THAT(cal.active(Channel::Coolant).interpolate(3500), WithinAbs(3.5, 1e-6));
  CHECK(cal.staged(Channel::Coolant) == nullptr);  // staging starts over

  // Other channels are untouched: provenance is per channel.
  CHECK_FALSE(cal.measured(Channel::Fuel));
  CHECK(cal.staged(Channel::Fuel) == nullptr);
}

TEST_CASE("Calibration: a failed save leaves the previous active curve and the staged points in place",
          "[firmware][calibration]") {
  // main.cpp only calls activate() after the NVS write succeeds; this is the state it must
  // be able to retry from.
  Calibration cal;
  for (std::size_t i = 0; i < Calibration::kMinimumPoints; ++i) {
    REQUIRE(cal.add(Channel::Volt, {static_cast<std::int32_t>(i * 1000), static_cast<float>(i)}));
  }
  const float placeholder_value = c3fw::placeholder_curves()[idx(Channel::Volt)].interpolate(16384);
  CHECK(cal.active(Channel::Volt).interpolate(16384) == placeholder_value);
  CHECK(cal.staged(Channel::Volt) != nullptr);
  CHECK_FALSE(cal.measured(Channel::Volt));
}

// ---------------------------------------------------------------------------
// placeholder_curves vs config/calibration.json
// ---------------------------------------------------------------------------

TEST_CASE("placeholder_curves: lookup.hpp matches the shipped calibration.json knot for knot",
          "[firmware][calibration]") {
  const std::string path = std::string(C3_PROJECT_ROOT) + "/config/calibration.json";
  const auto loaded = c3::telemetry::load_calibration(path);
  INFO(loaded.error);
  REQUIRE(loaded.table.has_value());
  CHECK(loaded.table->placeholder == c3fw::kPlaceholderCalibration);

  constexpr auto firmware = c3fw::placeholder_curves();
  for (std::size_t i = 0; i < kChannelCount; ++i) {
    const auto ch = static_cast<Channel>(i);
    INFO("channel " << c3::telemetry::to_string(ch));
    const auto& host = loaded.table->curve(ch).points();
    const auto& mcu = firmware[i].points();
    REQUIRE(mcu.size() == host.size());
    for (std::size_t k = 0; k < mcu.size(); ++k) {
      CHECK(mcu[k].raw == host[k].raw);
      CHECK(mcu[k].value == host[k].value);
    }
  }
}

TEST_CASE("placeholder_curves: every curve spans the ADC's positive range so the bench reads something everywhere",
          "[firmware][calibration]") {
  constexpr auto firmware = c3fw::placeholder_curves();
  STATIC_CHECK(c3fw::kFullScaleCounts == 32767);
  for (const Curve& c : firmware) {
    REQUIRE(c.size() == 2);
    CHECK(c.points().front().raw == 0);
    CHECK(c.points().back().raw == c3fw::kFullScaleCounts);
    CHECK(c.covers(0));
    CHECK(c.covers(c3fw::kFullScaleCounts));
  }
}
