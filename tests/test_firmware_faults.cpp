// The one place that turns a sensor value into a warning (AGENTS.md invariant 1): the spec §4.3
// set/clear table, the latch between the two levels, the sender-fault suppression rule, and
// the LED/buzzer derivation that drives GPIO regardless of the UART (invariant 2).
#include "c3fw/faults.hpp"
#include "c3fw/sender_check.hpp"
#include "telemetry/frame.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>

#include <array>
#include <cstddef>
#include <optional>

using c3::telemetry::Channel;
using c3::telemetry::Fault;
using c3::telemetry::FaultKind;
using c3fw::FaultEvaluator;
using c3fw::FaultList;
using c3fw::Reading;
using c3fw::SenderState;
using c3fw::Thresholds;
using c3fw::Warning;

CATCH_REGISTER_ENUM(FaultKind, FaultKind::OilLow, FaultKind::CltHigh, FaultKind::FuelLow, FaultKind::VoltLow,
                    FaultKind::VoltHigh, FaultKind::SenderOpen, FaultKind::SenderShort, FaultKind::Count)
CATCH_REGISTER_ENUM(Channel, Channel::Fuel, Channel::Oil, Channel::Coolant, Channel::Volt, Channel::Count)

namespace {

constexpr std::size_t idx(Channel ch) noexcept {
  return static_cast<std::size_t>(ch);
}

/// A warm, charging engine with every sender in range.
[[nodiscard]] Reading healthy() {
  Reading r;
  r.fuel_fraction = 0.5F;
  r.oil_psi = 40.0F;
  r.coolant_f = 190.0F;
  r.voltage_v = 14.2F;
  r.sender.fill(SenderState::Ok);
  return r;
}

[[nodiscard]] bool has(const FaultList& list, FaultKind kind, std::optional<Channel> channel = std::nullopt) {
  for (const Fault& f : list) {
    if (f.kind == kind && (!channel || f.channel == *channel)) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST_CASE("Thresholds: the defaults are the spec §4.3 table", "[firmware][faults]") {
  constexpr Thresholds t{};
  STATIC_CHECK(t.oil_low_set_psi == 12.0F);
  STATIC_CHECK(t.oil_low_clear_psi == 15.0F);
  STATIC_CHECK(t.clt_high_set_f == 235.0F);
  STATIC_CHECK(t.clt_high_clear_f == 228.0F);
  STATIC_CHECK(t.fuel_low_set == 0.08F);
  STATIC_CHECK(t.fuel_low_clear == 0.10F);
  STATIC_CHECK(t.volt_low_set == 12.4F);
  STATIC_CHECK(t.volt_low_clear == 12.8F);
  STATIC_CHECK(t.volt_high_set == 15.2F);
  STATIC_CHECK(t.volt_high_clear == 14.9F);
  STATIC_CHECK(t.engine_running_volts == 13.2F);
  // Every pair leaves a dead band, otherwise the latch would chatter.
  STATIC_CHECK(t.oil_low_set_psi < t.oil_low_clear_psi);
  STATIC_CHECK(t.clt_high_set_f > t.clt_high_clear_f);
  STATIC_CHECK(t.fuel_low_set < t.fuel_low_clear);
  STATIC_CHECK(t.volt_low_set < t.volt_low_clear);
  STATIC_CHECK(t.volt_high_set > t.volt_high_clear);
}

TEST_CASE("FaultEvaluator: OIL_LOW sets below 12 psi, clears at or above 15 and holds between", "[firmware][faults]") {
  FaultEvaluator ev;
  Reading r = healthy();
  CHECK_FALSE(has(ev.evaluate(r), FaultKind::OilLow));
  r.oil_psi = 12.0F;  // strictly below sets
  CHECK_FALSE(has(ev.evaluate(r), FaultKind::OilLow));
  r.oil_psi = 11.9F;
  CHECK(has(ev.evaluate(r), FaultKind::OilLow));
  CHECK(ev.oil_low());
  r.oil_psi = 13.5F;  // in the dead band: latched
  CHECK(has(ev.evaluate(r), FaultKind::OilLow));
  r.oil_psi = 14.99F;
  CHECK(has(ev.evaluate(r), FaultKind::OilLow));
  r.oil_psi = 15.0F;  // at or above clears
  CHECK_FALSE(has(ev.evaluate(r), FaultKind::OilLow));
  r.oil_psi = 13.5F;  // dead band again, now unlatched
  CHECK_FALSE(has(ev.evaluate(r), FaultKind::OilLow));
}

TEST_CASE("FaultEvaluator: CLT_HIGH sets above 235 °F, clears at or below 228 and holds between",
          "[firmware][faults]") {
  FaultEvaluator ev;
  Reading r = healthy();
  r.coolant_f = 235.0F;
  CHECK_FALSE(has(ev.evaluate(r), FaultKind::CltHigh));
  r.coolant_f = 235.1F;
  CHECK(has(ev.evaluate(r), FaultKind::CltHigh));
  CHECK(ev.clt_high());
  r.coolant_f = 230.0F;
  CHECK(has(ev.evaluate(r), FaultKind::CltHigh));
  r.coolant_f = 228.0F;
  CHECK_FALSE(has(ev.evaluate(r), FaultKind::CltHigh));
  r.coolant_f = 230.0F;
  CHECK_FALSE(has(ev.evaluate(r), FaultKind::CltHigh));
}

TEST_CASE("FaultEvaluator: FUEL_LOW, VOLT_LOW and VOLT_HIGH latch the same way", "[firmware][faults]") {
  FaultEvaluator ev;
  Reading r = healthy();

  r.fuel_fraction = 0.079F;
  CHECK(has(ev.evaluate(r), FaultKind::FuelLow));
  r.fuel_fraction = 0.09F;
  CHECK(has(ev.evaluate(r), FaultKind::FuelLow));
  r.fuel_fraction = 0.10F;
  CHECK_FALSE(has(ev.evaluate(r), FaultKind::FuelLow));

  r.voltage_v = 12.39F;
  CHECK(has(ev.evaluate(r), FaultKind::VoltLow));
  r.voltage_v = 12.6F;
  CHECK(has(ev.evaluate(r), FaultKind::VoltLow));
  r.voltage_v = 12.8F;
  CHECK_FALSE(has(ev.evaluate(r), FaultKind::VoltLow));

  r.voltage_v = 15.21F;
  CHECK(has(ev.evaluate(r), FaultKind::VoltHigh));
  r.voltage_v = 15.0F;
  CHECK(has(ev.evaluate(r), FaultKind::VoltHigh));
  r.voltage_v = 14.9F;
  CHECK_FALSE(has(ev.evaluate(r), FaultKind::VoltHigh));
}

TEST_CASE("FaultEvaluator: the list is in spec order, value faults then sender faults by channel",
          "[firmware][faults]") {
  FaultEvaluator ev;
  Reading r = healthy();
  r.oil_psi = 5.0F;
  r.coolant_f = 240.0F;
  r.fuel_fraction = 0.05F;
  r.voltage_v = 12.0F;
  const FaultList all = ev.evaluate(r);
  REQUIRE(all.size() == 4);
  CHECK(all[0].kind == FaultKind::OilLow);
  CHECK(all[1].kind == FaultKind::CltHigh);
  CHECK(all[2].kind == FaultKind::FuelLow);
  CHECK(all[3].kind == FaultKind::VoltLow);

  r.sender[idx(Channel::Fuel)] = SenderState::Open;
  r.sender[idx(Channel::Coolant)] = SenderState::Short;
  r.sender[idx(Channel::Volt)] = SenderState::Open;
  const FaultList mixed = ev.evaluate(r);
  REQUIRE(mixed.size() == 4);
  CHECK(mixed[0].kind == FaultKind::OilLow);  // oil is still readable
  CHECK(mixed[1].kind == FaultKind::SenderOpen);
  CHECK(mixed[1].channel == Channel::Fuel);
  CHECK(mixed[2].kind == FaultKind::SenderShort);
  CHECK(mixed[2].channel == Channel::Coolant);
  CHECK(mixed[3].kind == FaultKind::SenderOpen);
  CHECK(mixed[3].channel == Channel::Volt);
}

TEST_CASE("FaultEvaluator: a sender fault suppresses that channel's value fault and drops its latch",
          "[firmware][faults]") {
  FaultEvaluator ev;
  Reading r = healthy();
  r.oil_psi = 5.0F;
  CHECK(has(ev.evaluate(r), FaultKind::OilLow));

  r.sender[idx(Channel::Oil)] = SenderState::Open;
  r.oil_psi = 0.0F;  // whatever the lookup makes of an open input
  const FaultList open = ev.evaluate(r);
  CHECK_FALSE(has(open, FaultKind::OilLow));
  CHECK(has(open, FaultKind::SenderOpen, Channel::Oil));

  r.sender[idx(Channel::Oil)] = SenderState::Ok;
  r.oil_psi = 13.5F;  // dead band: the latch was dropped, so this does not re-arm it
  CHECK_FALSE(has(ev.evaluate(r), FaultKind::OilLow));
  r.oil_psi = 11.0F;
  CHECK(has(ev.evaluate(r), FaultKind::OilLow));
}

TEST_CASE("FaultEvaluator: every sender can fault at once and the list holds them all", "[firmware][faults]") {
  FaultEvaluator ev;
  Reading r = healthy();
  r.sender.fill(SenderState::Open);
  const FaultList all = ev.evaluate(r);
  REQUIRE(all.size() == c3::telemetry::kChannelCount);
  for (std::size_t i = 0; i < all.size(); ++i) {
    CHECK(all[i].kind == FaultKind::SenderOpen);
    CHECK(all[i].channel == static_cast<Channel>(i));
  }
}

// ---------------------------------------------------------------------------
// warning()
// ---------------------------------------------------------------------------

TEST_CASE("warning: a healthy engine shows nothing", "[firmware][faults]") {
  FaultEvaluator ev;
  static_cast<void>(ev.evaluate(healthy()));
  const Warning w = ev.warning();
  CHECK_FALSE(w.led);
  CHECK_FALSE(w.buzzer);
}

TEST_CASE("warning: CLT_HIGH lights the LED and sounds the buzzer unconditionally", "[firmware][faults]") {
  FaultEvaluator ev;
  Reading r = healthy();
  r.coolant_f = 240.0F;
  r.voltage_v = 12.0F;  // engine not charging; irrelevant for coolant
  static_cast<void>(ev.evaluate(r));
  CHECK(ev.warning().led);
  CHECK(ev.warning().buzzer);
}

TEST_CASE("warning: OIL_LOW lights the LED always and the buzzer only once the alternator charges",
          "[firmware][faults]") {
  FaultEvaluator ev;
  Reading r = healthy();
  r.oil_psi = 0.0F;
  r.voltage_v = 12.5F;  // key on, engine off
  static_cast<void>(ev.evaluate(r));
  CHECK(ev.warning().led);
  CHECK_FALSE(ev.warning().buzzer);
  CHECK_FALSE(ev.engine_running());

  r.voltage_v = 13.19F;
  static_cast<void>(ev.evaluate(r));
  CHECK_FALSE(ev.warning().buzzer);

  r.voltage_v = 13.2F;  // charging: the engine is running and has no oil pressure
  static_cast<void>(ev.evaluate(r));
  CHECK(ev.warning().led);
  CHECK(ev.warning().buzzer);
  CHECK(ev.engine_running());
}

TEST_CASE("warning: an unreadable voltage channel does not mute the oil buzzer", "[firmware][faults]") {
  FaultEvaluator ev;
  Reading r = healthy();
  r.oil_psi = 0.0F;
  r.voltage_v = 0.0F;  // the lookup of a broken divider
  r.sender[idx(Channel::Volt)] = SenderState::Open;
  const FaultList list = ev.evaluate(r);
  CHECK_FALSE(has(list, FaultKind::VoltLow));  // no value fault on a faulted channel
  CHECK(has(list, FaultKind::SenderOpen, Channel::Volt));
  CHECK(ev.engine_running());  // unknown fails toward sounding
  CHECK(ev.warning().led);
  CHECK(ev.warning().buzzer);
}

TEST_CASE("warning: an unreadable oil or coolant sender lights the LED but not the buzzer", "[firmware][faults]") {
  FaultEvaluator ev;
  Reading r = healthy();

  r.sender[idx(Channel::Oil)] = SenderState::Open;
  static_cast<void>(ev.evaluate(r));
  CHECK(ev.warning().led);
  CHECK_FALSE(ev.warning().buzzer);

  r.sender[idx(Channel::Oil)] = SenderState::Ok;
  r.sender[idx(Channel::Coolant)] = SenderState::Short;
  static_cast<void>(ev.evaluate(r));
  CHECK(ev.warning().led);
  CHECK_FALSE(ev.warning().buzzer);

  // Fuel and volt sender faults are not warning-path channels.
  r.sender.fill(SenderState::Ok);
  r.sender[idx(Channel::Fuel)] = SenderState::Open;
  r.sender[idx(Channel::Volt)] = SenderState::Open;
  static_cast<void>(ev.evaluate(r));
  CHECK_FALSE(ev.warning().led);
  CHECK_FALSE(ev.warning().buzzer);

  r.sender.fill(SenderState::Ok);
  static_cast<void>(ev.evaluate(r));
  CHECK_FALSE(ev.warning().led);
}

TEST_CASE("FaultEvaluator: custom thresholds are honoured", "[firmware][faults]") {
  Thresholds t{};
  t.oil_low_set_psi = 20.0F;
  t.oil_low_clear_psi = 25.0F;
  FaultEvaluator ev(t);
  Reading r = healthy();
  r.oil_psi = 18.0F;
  CHECK(has(ev.evaluate(r), FaultKind::OilLow));
  r.oil_psi = 25.0F;
  CHECK_FALSE(has(ev.evaluate(r), FaultKind::OilLow));
  CHECK(ev.thresholds().oil_low_set_psi == 20.0F);
}

TEST_CASE("FaultEvaluator: usable at compile time, so the warning rules are pure", "[firmware][faults]") {
  constexpr auto buzzer_for = [](float oil_psi, float volts) {
    FaultEvaluator ev;
    Reading r;
    r.oil_psi = oil_psi;
    r.coolant_f = 190.0F;
    r.fuel_fraction = 0.5F;
    r.voltage_v = volts;
    static_cast<void>(ev.evaluate(r));
    return ev.warning().buzzer;
  };
  STATIC_CHECK_FALSE(buzzer_for(0.0F, 12.5F));
  STATIC_CHECK(buzzer_for(0.0F, 14.0F));
  STATIC_CHECK_FALSE(buzzer_for(40.0F, 14.0F));
}
