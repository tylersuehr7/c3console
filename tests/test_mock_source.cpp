#include "telemetry/calibration.hpp"
#include "telemetry/frame.hpp"
#include "telemetry/limits.hpp"
#include "telemetry/mock_source.hpp"
#include "telemetry/sensor_source.hpp"
#include "vehicle/vehicle_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

using c3::telemetry::Channel;
using c3::telemetry::Fault;
using c3::telemetry::FaultKind;
using c3::telemetry::Frame;
using c3::telemetry::kChannelCount;
using c3::telemetry::LinkState;
using c3::telemetry::MockFaults;
using c3::telemetry::MockLoads;
using c3::telemetry::MockProfile;
using c3::telemetry::MockSource;
using c3::telemetry::parse_mock_profile;
using c3::telemetry::SensorSource;
using c3::telemetry::SourceHealth;
using c3::vehicle::HvacBlower;
using c3::vehicle::HvacMode;
using Catch::Matchers::WithinAbs;

CATCH_REGISTER_ENUM(LinkState, LinkState::NoData, LinkState::Live, LinkState::Stalled, LinkState::Failed)
CATCH_REGISTER_ENUM(FaultKind, FaultKind::OilLow, FaultKind::CltHigh, FaultKind::FuelLow, FaultKind::VoltLow,
                    FaultKind::VoltHigh, FaultKind::SenderOpen, FaultKind::SenderShort, FaultKind::Count)

namespace {

namespace limits = c3::telemetry::limits;

// ---------------------------------------------------------------------------
// Fake clock. MonotonicClock is a plain function pointer, so the time source is file-scope
// state that each test case sets before polling. Catch2 runs cases one at a time.
// ---------------------------------------------------------------------------

std::uint64_t g_now_ms = 0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

[[nodiscard]] std::uint64_t fake_now() noexcept {
  return g_now_ms;
}

constexpr std::uint32_t kPeriod = MockSource::kPeriodMs;

[[nodiscard]] MockSource make(MockProfile profile, std::uint64_t start_ms = 1'000'000) {
  g_now_ms = start_ms;
  return MockSource(profile, &fake_now);
}

[[nodiscard]] std::optional<Frame> poll_at(MockSource& source, std::uint64_t t_ms) {
  g_now_ms = t_ms;
  return source.poll();
}

[[nodiscard]] Frame frame_at(MockSource& source, std::uint64_t t_ms) {
  const auto frame = poll_at(source, t_ms);
  INFO("poll at " << t_ms << " ms returned no frame");
  REQUIRE(frame.has_value());
  return *frame;
}

/// Polls once per period for `seconds` of wall time, starting after the current instant,
/// and returns the last frame delivered. Every poll is expected to yield a frame.
[[nodiscard]] Frame run_for(MockSource& source, double seconds) {
  const auto polls = static_cast<std::uint64_t>(seconds * 1000.0 / kPeriod);
  std::optional<Frame> last;
  for (std::uint64_t i = 0; i < polls; ++i) {
    g_now_ms += kPeriod;
    const auto f = source.poll();
    REQUIRE(f.has_value());
    last = f;
  }
  REQUIRE(last.has_value());
  return *last;
}

[[nodiscard]] bool has_fault(const Frame& f, FaultKind kind) noexcept {
  return std::any_of(f.faults.begin(), f.faults.end(), [kind](const Fault& x) { return x.kind == kind; });
}

[[nodiscard]] bool has_fault(const Frame& f, FaultKind kind, Channel channel) noexcept {
  return std::any_of(f.faults.begin(), f.faults.end(),
                     [&](const Fault& x) { return x.kind == kind && x.channel == channel; });
}

[[nodiscard]] bool within_limits(const Frame& f) noexcept {
  return f.fuel_fraction >= limits::kFuelMin && f.fuel_fraction <= limits::kFuelMax &&
         f.oil_psi >= limits::kOilMinPsi && f.oil_psi <= limits::kOilMaxPsi && f.coolant_f >= limits::kCltMinF &&
         f.coolant_f <= limits::kCltMaxF && f.voltage_v >= limits::kVoltMin && f.voltage_v <= limits::kVoltMax;
}

[[nodiscard]] bool same_faults(const Frame& a, const Frame& b) noexcept {
  if (a.faults.size() != b.faults.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.faults.size(); ++i) {
    if (a.faults[i].kind != b.faults[i].kind || a.faults[i].channel != b.faults[i].channel) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool same_model_output(const Frame& a, const Frame& b) noexcept {
  return a.fuel_fraction == b.fuel_fraction && a.oil_psi == b.oil_psi && a.coolant_f == b.coolant_f &&
         a.voltage_v == b.voltage_v && a.raw == b.raw && same_faults(a, b) && a.reverse == b.reverse &&
         a.relays.has_value() == b.relays.has_value();
}

constexpr std::array<MockProfile, 9> kAllProfiles{
    MockProfile::ColdStart,     MockProfile::WarmupDrive,     MockProfile::HotIdle,
    MockProfile::HighwayCruise, MockProfile::OilPressureLoss, MockProfile::Overheat,
    MockProfile::SenderOpen,    MockProfile::AlternatorFail,  MockProfile::LinkLoss,
};

// ---------------------------------------------------------------------------
// Profiles: names and constants
// ---------------------------------------------------------------------------

TEST_CASE("MockProfile: to_string and parse_mock_profile round-trip every profile", "[mock_source]") {
  for (const auto profile : kAllProfiles) {
    const auto name = to_string(profile);
    INFO("profile: " << name);
    CHECK_FALSE(name.empty());
    CHECK(name != "?");
    const auto parsed = parse_mock_profile(name);
    REQUIRE(parsed.has_value());
    CHECK(*parsed == profile);
  }
}

TEST_CASE("MockProfile: the spec's nine profile names are all present", "[mock_source]") {
  for (const std::string_view name : {"ColdStart", "WarmupDrive", "HotIdle", "HighwayCruise", "OilPressureLoss",
                                      "Overheat", "SenderOpen", "AlternatorFail", "LinkLoss"}) {
    INFO("name: " << name);
    CHECK(parse_mock_profile(name).has_value());
  }
  STATIC_CHECK(static_cast<std::size_t>(MockProfile::Count) == 9);
}

TEST_CASE("parse_mock_profile: is case-sensitive and rejects anything else", "[mock_source]") {
  CHECK_FALSE(parse_mock_profile("").has_value());
  CHECK_FALSE(parse_mock_profile("coldstart").has_value());
  CHECK_FALSE(parse_mock_profile("COLDSTART").has_value());
  CHECK_FALSE(parse_mock_profile("ColdStart ").has_value());
  CHECK_FALSE(parse_mock_profile(" ColdStart").has_value());
  CHECK_FALSE(parse_mock_profile("Cold").has_value());
  CHECK_FALSE(parse_mock_profile("Count").has_value());
  CHECK_FALSE(parse_mock_profile("?").has_value());
  CHECK_FALSE(parse_mock_profile("Replay").has_value());
}

TEST_CASE("MockSource: constants match the spec's 10 Hz frame and 10 s link loss", "[mock_source]") {
  STATIC_CHECK(MockSource::kPeriodMs == 100);
  STATIC_CHECK(MockSource::kLinkLossAfterS == 10.0);
  STATIC_CHECK(MockSource::kMaxCatchUpTicks == 50);
}

TEST_CASE("MockSource: implements SensorSource with no descriptor and noexcept polling", "[mock_source]") {
  STATIC_CHECK(std::is_base_of_v<SensorSource, MockSource>);
  STATIC_CHECK_FALSE(std::is_copy_constructible_v<MockSource>);
  STATIC_CHECK_FALSE(std::is_move_constructible_v<MockSource>);
  MockSource source = make(MockProfile::ColdStart);
  STATIC_CHECK(noexcept(source.poll()));
  STATIC_CHECK(noexcept(source.health()));
  CHECK(source.descriptor() == -1);
  CHECK(source.profile() == MockProfile::ColdStart);
}

// ---------------------------------------------------------------------------
// Polling and timing
// ---------------------------------------------------------------------------

TEST_CASE("poll: before the first poll the link reports no data", "[mock_source]") {
  const MockSource source = make(MockProfile::WarmupDrive);
  const SourceHealth h = source.health();
  CHECK(h.state == LinkState::NoData);
  CHECK(h.frames_ok == 0);
  CHECK(h.frames_bad_crc == 0);
  CHECK(h.frames_malformed == 0);
  CHECK(h.last_frame_monotonic_ms == 0);
}

TEST_CASE("poll: the first poll emits tick 1 immediately regardless of the clock value", "[mock_source]") {
  for (const std::uint64_t start :
       {std::uint64_t{0}, std::uint64_t{37}, std::uint64_t{1'000'000}, std::uint64_t{0xFFFF'FFFF'0000ULL}}) {
    MockSource source = make(MockProfile::WarmupDrive, start);
    const Frame f = frame_at(source, start);
    CHECK(f.t_ms == kPeriod);
    const SourceHealth h = source.health();
    CHECK(h.state == LinkState::Live);
    CHECK(h.frames_ok == 1);
    CHECK(h.last_frame_monotonic_ms == start);
  }
}

TEST_CASE("poll: nothing new before a full period has elapsed", "[mock_source]") {
  MockSource source = make(MockProfile::WarmupDrive, 1000);
  REQUIRE(frame_at(source, 1000).t_ms == 100);
  CHECK_FALSE(poll_at(source, 1000).has_value());
  CHECK_FALSE(poll_at(source, 1050).has_value());
  CHECK_FALSE(poll_at(source, 1099).has_value());
  const Frame f = frame_at(source, 1100);
  CHECK(f.t_ms == 200);
}

TEST_CASE("poll: an empty poll leaves health untouched", "[mock_source]") {
  MockSource source = make(MockProfile::WarmupDrive, 1000);
  static_cast<void>(frame_at(source, 1000));
  const SourceHealth before = source.health();
  CHECK_FALSE(poll_at(source, 1050).has_value());
  const SourceHealth after = source.health();
  CHECK(after.state == before.state);
  CHECK(after.frames_ok == before.frames_ok);
  CHECK(after.last_frame_monotonic_ms == before.last_frame_monotonic_ms);
}

TEST_CASE("poll: t_ms advances by exactly one period per tick", "[mock_source]") {
  MockSource source = make(MockProfile::HighwayCruise, 0);
  std::uint32_t expected = 0;
  for (std::uint64_t now = 0; now <= 5000; now += kPeriod) {
    expected += kPeriod;
    CHECK(frame_at(source, now).t_ms == expected);
  }
}

TEST_CASE("poll: catches up several ticks in one call and returns the latest", "[mock_source]") {
  MockSource source = make(MockProfile::WarmupDrive, 0);
  REQUIRE(frame_at(source, 0).t_ms == 100);
  const Frame f = frame_at(source, 1000);  // ticks 2..11 are due
  CHECK(f.t_ms == 1100);
  CHECK(source.health().frames_ok == 2);  // one delivered frame per successful poll, not per tick
  CHECK(source.health().last_frame_monotonic_ms == 1000);
}

TEST_CASE("poll: the model is independent of poll cadence", "[mock_source]") {
  // A is polled every period; B once at the start and once 4.9 s later. Both have stepped
  // the model exactly 50 times when they emit tick 50, so the outputs must be identical.
  MockSource a = make(MockProfile::WarmupDrive, 0);
  Frame fa = frame_at(a, 0);
  for (std::uint64_t now = kPeriod; now < 50 * kPeriod; now += kPeriod) {
    fa = frame_at(a, now);
  }
  REQUIRE(fa.t_ms == 5000);

  MockSource b = make(MockProfile::WarmupDrive, 0);
  static_cast<void>(frame_at(b, 0));
  const Frame fb = frame_at(b, 49 * kPeriod);
  REQUIRE(fb.t_ms == 5000);

  CHECK(same_model_output(fa, fb));
  CHECK(a.health().frames_ok == 50);
  CHECK(b.health().frames_ok == 2);
}

TEST_CASE("poll: a long pause is bounded to kMaxCatchUpTicks of simulation", "[mock_source]") {
  // After the gap the wall clock says 100 s passed, but the model only advanced 50 ticks.
  MockSource paused = make(MockProfile::WarmupDrive, 0);
  static_cast<void>(frame_at(paused, 0));
  const Frame after_gap = frame_at(paused, 100'000);
  CHECK(after_gap.t_ms == 100'100);  // tick counter skips the dropped gap

  MockSource steady = make(MockProfile::WarmupDrive, 0);
  Frame reference = frame_at(steady, 0);
  for (std::uint64_t now = kPeriod; now <= 50 * kPeriod; now += kPeriod) {
    reference = frame_at(steady, now);  // 51 steps in total, same as 1 + 50 caught up
  }
  REQUIRE(reference.t_ms == 5100);
  CHECK(same_model_output(after_gap, reference));
}

TEST_CASE("poll: a pause of exactly kMaxCatchUpTicks is simulated in full", "[mock_source]") {
  MockSource source = make(MockProfile::WarmupDrive, 0);
  static_cast<void>(frame_at(source, 0));
  const Frame f = frame_at(source, 50 * kPeriod);
  CHECK(f.t_ms == 5100);
}

TEST_CASE("poll: a clock that steps backwards restarts the tick timeline without resetting the model",
          "[mock_source]") {
  MockSource source = make(MockProfile::WarmupDrive, 5000);
  static_cast<void>(frame_at(source, 5000));
  const Frame before = frame_at(source, 7000);  // 21 ticks in
  REQUIRE(before.t_ms == 2100);

  const Frame after = frame_at(source, 1000);          // clock went backwards
  CHECK(after.t_ms == kPeriod);                        // timeline restarted
  CHECK(after.fuel_fraction <= before.fuel_fraction);  // model kept draining; it was not reset
  CHECK(after.coolant_f >= before.coolant_f);
  CHECK(source.health().last_frame_monotonic_ms == 1000);
}

TEST_CASE("poll: two sources with the same profile and clock produce identical frames", "[mock_source]") {
  MockSource a = make(MockProfile::OilPressureLoss, 0);
  MockSource b = make(MockProfile::OilPressureLoss, 0);
  for (std::uint64_t now = 0; now < 60'000; now += kPeriod) {
    const Frame fa = frame_at(a, now);
    const Frame fb = frame_at(b, now);
    REQUIRE(fa.t_ms == fb.t_ms);
    REQUIRE(same_model_output(fa, fb));
  }
}

// ---------------------------------------------------------------------------
// Frame contents
// ---------------------------------------------------------------------------

TEST_CASE("frame: every profile stays inside the plausibility limits for an hour", "[mock_source]") {
  for (const auto profile : kAllProfiles) {
    INFO("profile: " << to_string(profile));
    MockSource source = make(profile, 0);
    std::size_t delivered = 0;
    for (std::uint64_t now = 0; now < 3'600'000; now += kPeriod) {
      const auto f = poll_at(source, now);
      if (!f) {
        continue;  // LinkLoss goes silent by design
      }
      ++delivered;
      if (!within_limits(*f)) {
        INFO("t_ms=" << f->t_ms << " fuel=" << f->fuel_fraction << " oil=" << f->oil_psi << " clt=" << f->coolant_f
                     << " volt=" << f->voltage_v);
        FAIL("frame outside plausibility limits");
      }
    }
    CHECK(delivered > 0);
  }
}

TEST_CASE("frame: reverse is never reported by the mock", "[mock_source]") {
  MockSource source = make(MockProfile::WarmupDrive, 0);
  for (std::uint64_t now = 0; now < 5000; now += kPeriod) {
    CHECK_FALSE(frame_at(source, now).reverse);
  }
}

TEST_CASE("frame: relays echo the loads the UI set, including doors and high beams", "[mock_source]") {
  MockSource source = make(MockProfile::WarmupDrive, 0);
  const Frame defaults = frame_at(source, 0);
  REQUIRE(defaults.relays.has_value());
  CHECK(defaults.relays->blower == HvacBlower::Off);
  CHECK(defaults.relays->mode == HvacMode::Vent);
  CHECK_FALSE(defaults.relays->ac_enabled);
  CHECK_FALSE(defaults.relays->headlights_on);
  REQUIRE(defaults.relays->lamp_doors_open.has_value());
  CHECK_FALSE(*defaults.relays->lamp_doors_open);
  REQUIRE(defaults.relays->highbeams_on.has_value());
  CHECK_FALSE(*defaults.relays->highbeams_on);

  source.set_loads(MockLoads{
      .blower = HvacBlower::L3,
      .mode = HvacMode::Defrost,
      .ac = true,
      .headlights = true,
      .doors = true,
      .high = true,
  });
  const Frame loaded = frame_at(source, kPeriod);
  REQUIRE(loaded.relays.has_value());
  CHECK(loaded.relays->blower == HvacBlower::L3);
  CHECK(loaded.relays->mode == HvacMode::Defrost);
  CHECK(loaded.relays->ac_enabled);
  CHECK(loaded.relays->headlights_on);
  CHECK(*loaded.relays->lamp_doors_open);
  CHECK(*loaded.relays->highbeams_on);
}

TEST_CASE("frame: raw counts invert the shipped placeholder calibration table", "[mock_source]") {
  const auto cal = c3::telemetry::load_calibration(std::string(C3_PROJECT_ROOT) + "/config/calibration.json");
  INFO("calibration error: " << cal.error);
  REQUIRE(cal.table.has_value());

  for (const auto profile : {MockProfile::ColdStart, MockProfile::WarmupDrive, MockProfile::HotIdle,
                             MockProfile::Overheat, MockProfile::AlternatorFail}) {
    INFO("profile: " << to_string(profile));
    MockSource source = make(profile, 0);
    Frame f = frame_at(source, 0);
    f = run_for(source, 30.0);

    struct Check {
      Channel ch;
      float value;
      float lo;
      float hi;
    };
    const Check checks[] = {
        {Channel::Fuel, f.fuel_fraction, limits::kFuelMin, limits::kFuelMax},
        {Channel::Oil, f.oil_psi, limits::kOilMinPsi, limits::kOilMaxPsi},
        {Channel::Coolant, f.coolant_f, limits::kCltMinF, limits::kCltMaxF},
        {Channel::Volt, f.voltage_v, limits::kVoltMin, limits::kVoltMax},
    };
    for (const auto& c : checks) {
      INFO("channel: " << to_string(c.ch));
      const auto raw = f.raw[static_cast<std::size_t>(c.ch)];
      CHECK(raw >= 0);
      CHECK(raw <= 32767);
      const double one_count = (static_cast<double>(c.hi) - c.lo) / 32767.0;
      CHECK_THAT(cal.table->convert(c.ch, raw), WithinAbs(c.value, one_count + 1e-3));
    }
  }
}

TEST_CASE("frame: raw counts are clamped to the ADC span", "[mock_source]") {
  MockSource source = make(MockProfile::ColdStart, 0);
  const Frame f = frame_at(source, 0);
  for (const auto raw : f.raw) {
    CHECK(raw >= 0);
    CHECK(raw <= 32767);
  }
  // Fuel at 0.62 of span: the inverse of the placeholder table's 0..1 line.
  CHECK(f.raw[static_cast<std::size_t>(Channel::Fuel)] == static_cast<std::int32_t>(f.fuel_fraction * 32767.0F));
}

// ---------------------------------------------------------------------------
// Physics per profile (spec §9.1). Ranges come from running the step model; they are
// deliberately wide so the test pins behaviour, not rounding.
// ---------------------------------------------------------------------------

TEST_CASE("physics: initial state is a cold engine that just started", "[mock_source]") {
  MockSource source = make(MockProfile::ColdStart, 0);
  const Frame f = frame_at(source, 0);
  CHECK_THAT(f.coolant_f, WithinAbs(72.0, 1.0));
  CHECK_THAT(f.fuel_fraction, WithinAbs(0.62, 0.001));
  CHECK_THAT(f.voltage_v, WithinAbs(14.2, 0.1));
  CHECK(f.oil_psi > 0.0F);
  CHECK(f.oil_psi < 30.0F);  // pump just started building pressure
  CHECK(f.faults.empty());
}

TEST_CASE("physics: ColdStart idles cold with high oil pressure and no faults", "[mock_source]") {
  MockSource source = make(MockProfile::ColdStart, 0);
  static_cast<void>(frame_at(source, 0));
  const Frame f = run_for(source, 120.0);
  CHECK_THAT(f.coolant_f, WithinAbs(70.0, 2.0));
  CHECK(f.oil_psi > 40.0F);  // cold oil is thick
  CHECK(f.oil_psi < 50.0F);
  CHECK_THAT(f.voltage_v, WithinAbs(14.33, 0.05));
  CHECK(f.faults.empty());
}

TEST_CASE("physics: WarmupDrive warms toward the thermostat and oil settles near 40 psi", "[mock_source]") {
  MockSource source = make(MockProfile::WarmupDrive, 0);
  Frame previous = frame_at(source, 0);
  bool coolant_monotonic = true;
  Frame f = previous;
  for (int i = 0; i < 1200; ++i) {  // 120 s
    f = frame_at(source, static_cast<std::uint64_t>(i + 1) * kPeriod);
    coolant_monotonic = coolant_monotonic && f.coolant_f >= previous.coolant_f;
    previous = f;
  }
  CHECK(coolant_monotonic);
  CHECK(f.coolant_f > 185.0F);
  CHECK(f.coolant_f < 200.0F);
  CHECK(f.oil_psi > 35.0F);
  CHECK(f.oil_psi < 55.0F);
  CHECK_THAT(f.voltage_v, WithinAbs(14.33, 0.05));
  CHECK(f.faults.empty());
}

TEST_CASE("physics: HotIdle runs hotter with low but healthy oil pressure", "[mock_source]") {
  MockSource source = make(MockProfile::HotIdle, 0);
  static_cast<void>(frame_at(source, 0));
  const Frame f = run_for(source, 300.0);
  CHECK_THAT(f.coolant_f, WithinAbs(214.0, 2.0));
  CHECK(f.oil_psi > 15.0F);  // above the OIL_LOW clear threshold
  CHECK(f.oil_psi < 25.0F);
  CHECK(f.faults.empty());
}

TEST_CASE("physics: HighwayCruise is cooler than idle thanks to road speed", "[mock_source]") {
  MockSource highway = make(MockProfile::HighwayCruise, 0);
  static_cast<void>(frame_at(highway, 0));
  const Frame h = run_for(highway, 300.0);

  MockSource idle = make(MockProfile::HotIdle, 0);
  static_cast<void>(frame_at(idle, 0));
  const Frame i = run_for(idle, 300.0);

  CHECK(h.coolant_f < i.coolant_f);
  CHECK(h.oil_psi > i.oil_psi);
  CHECK(h.oil_psi > 38.0F);
  CHECK(h.oil_psi < 50.0F);
  CHECK(h.faults.empty());
}

TEST_CASE("physics: fuel drains monotonically and never goes negative", "[mock_source]") {
  for (const auto profile : {MockProfile::ColdStart, MockProfile::WarmupDrive, MockProfile::HighwayCruise}) {
    INFO("profile: " << to_string(profile));
    MockSource source = make(profile, 0);
    Frame previous = frame_at(source, 0);
    bool monotonic = true;
    for (int i = 1; i <= 3000; ++i) {
      const Frame f = frame_at(source, static_cast<std::uint64_t>(i) * kPeriod);
      monotonic = monotonic && f.fuel_fraction <= previous.fuel_fraction && f.fuel_fraction >= 0.0F;
      previous = f;
    }
    CHECK(monotonic);
    CHECK(previous.fuel_fraction < 0.62F);
  }
}

TEST_CASE("physics: WarmupDrive drains a tank in about an hour, not the browser demo's 40 s", "[mock_source]") {
  MockSource source = make(MockProfile::WarmupDrive, 0);
  static_cast<void>(frame_at(source, 0));
  const Frame f = run_for(source, 600.0);
  CHECK_THAT(f.fuel_fraction, WithinAbs(0.62 - 0.00028 * 600.0, 0.002));
}

TEST_CASE("physics: driving long enough eventually raises FUEL_LOW", "[mock_source]") {
  MockSource source = make(MockProfile::WarmupDrive, 0);
  static_cast<void>(frame_at(source, 0));
  std::optional<std::uint32_t> first_low_at;
  Frame f;
  for (int i = 1; i <= 21'000; ++i) {  // 35 minutes
    f = frame_at(source, static_cast<std::uint64_t>(i) * kPeriod);
    if (!first_low_at && has_fault(f, FaultKind::FuelLow)) {
      first_low_at = f.t_ms;
    }
  }
  REQUIRE(first_low_at.has_value());
  CHECK(*first_low_at > 1'800'000U);  // well past 30 minutes
  CHECK(*first_low_at < 2'000'000U);
  CHECK(f.fuel_fraction < 0.08F);
  CHECK(f.fuel_fraction >= 0.0F);
  CHECK(has_fault(f, FaultKind::FuelLow));
}

TEST_CASE("physics: electrical load sags the charging voltage", "[mock_source]") {
  MockSource unloaded = make(MockProfile::WarmupDrive, 0);
  static_cast<void>(frame_at(unloaded, 0));
  const Frame u = run_for(unloaded, 10.0);

  MockSource loaded = make(MockProfile::WarmupDrive, 0);
  loaded.set_loads(MockLoads{.blower = HvacBlower::L4, .ac = true, .headlights = true});
  static_cast<void>(frame_at(loaded, 0));
  const Frame l = run_for(loaded, 10.0);

  CHECK(l.voltage_v < u.voltage_v);
  CHECK(u.voltage_v - l.voltage_v > 0.1F);
  CHECK(l.voltage_v > 14.0F);  // still charging; no VOLT_LOW under normal load
  CHECK(l.faults.empty());
  CHECK(u.faults.empty());
}

TEST_CASE("physics: changing loads mid-run moves the voltage toward the new target", "[mock_source]") {
  MockSource source = make(MockProfile::WarmupDrive, 0);
  static_cast<void>(frame_at(source, 0));
  const Frame before = run_for(source, 10.0);
  source.set_loads(MockLoads{.blower = HvacBlower::L4, .ac = true, .headlights = true});
  const Frame after = run_for(source, 10.0);
  CHECK(after.voltage_v < before.voltage_v);
  source.set_loads(MockLoads{});
  const Frame recovered = run_for(source, 10.0);
  CHECK_THAT(recovered.voltage_v, WithinAbs(before.voltage_v, 0.01));
}

// ---------------------------------------------------------------------------
// Fault profiles
// ---------------------------------------------------------------------------

TEST_CASE("OilPressureLoss: oil pressure collapses periodically and OIL_LOW comes and goes", "[mock_source]") {
  MockSource source = make(MockProfile::OilPressureLoss, 0);
  std::size_t low_ticks = 0;
  std::size_t clear_ticks = 0;
  float min_oil = 1000.0F;
  std::optional<std::uint32_t> first_low_at;
  for (std::uint64_t now = 0; now < 60'000; now += kPeriod) {
    const Frame f = frame_at(source, now);
    min_oil = std::min(min_oil, f.oil_psi);
    if (has_fault(f, FaultKind::OilLow)) {
      ++low_ticks;
      if (!first_low_at) {
        first_low_at = f.t_ms;
      }
      CHECK(f.oil_psi < 15.0F);  // a latched fault only persists below the clear threshold
    } else {
      ++clear_ticks;
    }
  }
  REQUIRE(first_low_at.has_value());
  CHECK(*first_low_at < 20'000U);  // the first collapse lands inside the first 22 s cycle
  CHECK(min_oil < 5.0F);
  CHECK(low_ticks > 100);
  CHECK(clear_ticks > 100);
}

TEST_CASE("OilPressureLoss: never reports a sender fault", "[mock_source]") {
  MockSource source = make(MockProfile::OilPressureLoss, 0);
  for (std::uint64_t now = 0; now < 30'000; now += kPeriod) {
    const Frame f = frame_at(source, now);
    CHECK_FALSE(has_fault(f, FaultKind::SenderOpen));
    CHECK(f.raw[static_cast<std::size_t>(Channel::Oil)] >= 0);
  }
}

TEST_CASE("Overheat: coolant climbs past the alarm point and CLT_HIGH latches", "[mock_source]") {
  MockSource source = make(MockProfile::Overheat, 0);
  std::optional<std::uint32_t> first_high_at;
  Frame f;
  for (std::uint64_t now = 0; now < 120'000; now += kPeriod) {
    f = frame_at(source, now);
    if (!first_high_at && has_fault(f, FaultKind::CltHigh)) {
      first_high_at = f.t_ms;
      CHECK(f.coolant_f > 235.0F);
    }
    if (first_high_at) {
      CHECK(has_fault(f, FaultKind::CltHigh));  // it stays hot; the fault must not flicker
    }
  }
  REQUIRE(first_high_at.has_value());
  CHECK(*first_high_at > 30'000U);  // a real warm-up, not an instant alarm
  CHECK(*first_high_at < 60'000U);
  CHECK_THAT(f.coolant_f, WithinAbs(251.0, 1.0));
  CHECK(f.coolant_f <= limits::kCltMaxF);
}

TEST_CASE("Overheat: everything except coolant behaves like WarmupDrive", "[mock_source]") {
  MockSource overheat = make(MockProfile::Overheat, 0);
  MockSource warmup = make(MockProfile::WarmupDrive, 0);
  static_cast<void>(frame_at(overheat, 0));
  static_cast<void>(frame_at(warmup, 0));
  const Frame o = run_for(overheat, 5.0);
  g_now_ms = 0;
  const Frame w = run_for(warmup, 5.0);
  CHECK(o.fuel_fraction == w.fuel_fraction);
  CHECK(o.voltage_v == w.voltage_v);
  CHECK(o.coolant_f > w.coolant_f);
}

TEST_CASE("SenderOpen: oil reads zero, raw is zero, and only the sender fault is raised for oil", "[mock_source]") {
  MockSource source = make(MockProfile::SenderOpen, 0);
  for (std::uint64_t now = 0; now < 60'000; now += kPeriod) {
    const Frame f = frame_at(source, now);
    CHECK(f.oil_psi == 0.0F);
    CHECK(f.raw[static_cast<std::size_t>(Channel::Oil)] == 0);
    CHECK(has_fault(f, FaultKind::SenderOpen, Channel::Oil));
    CHECK_FALSE(has_fault(f, FaultKind::OilLow));  // an open sender is a wiring fault, not low pressure
  }
}

TEST_CASE("SenderOpen: the sender fault is the last entry and other channels are unaffected", "[mock_source]") {
  MockSource source = make(MockProfile::SenderOpen, 0);
  static_cast<void>(frame_at(source, 0));
  const Frame f = run_for(source, 30.0);
  REQUIRE_FALSE(f.faults.empty());
  CHECK(f.faults.back().kind == FaultKind::SenderOpen);
  CHECK(f.faults.back().channel == Channel::Oil);
  CHECK(f.coolant_f > 100.0F);
  CHECK(f.voltage_v > 14.0F);
  CHECK(f.fuel_fraction > 0.6F);
}

TEST_CASE("AlternatorFail: voltage falls to battery level and VOLT_LOW is raised", "[mock_source]") {
  MockSource source = make(MockProfile::AlternatorFail, 0);
  static_cast<void>(frame_at(source, 0));
  const Frame f = run_for(source, 5.0);
  CHECK_THAT(f.voltage_v, WithinAbs(11.53, 0.1));
  CHECK(has_fault(f, FaultKind::VoltLow));
  CHECK_FALSE(has_fault(f, FaultKind::VoltHigh));
  CHECK(f.voltage_v >= limits::kVoltMin);
}

TEST_CASE("AlternatorFail: load drags the dying battery lower still", "[mock_source]") {
  MockSource loaded = make(MockProfile::AlternatorFail, 0);
  loaded.set_loads(MockLoads{.blower = HvacBlower::L4, .ac = true, .headlights = true});
  static_cast<void>(frame_at(loaded, 0));
  const Frame f = run_for(loaded, 5.0);
  CHECK(f.voltage_v < 11.4F);
  CHECK(f.voltage_v > 10.0F);
  CHECK(has_fault(f, FaultKind::VoltLow));
}

TEST_CASE("VOLT_HIGH: is never raised by any profile under any load", "[mock_source]") {
  for (const auto profile : kAllProfiles) {
    INFO("profile: " << to_string(profile));
    MockSource source = make(profile, 0);
    source.set_loads(MockLoads{});
    for (std::uint64_t now = 0; now < 30'000; now += kPeriod) {
      const auto f = poll_at(source, now);
      if (f) {
        CHECK_FALSE(has_fault(*f, FaultKind::VoltHigh));
      }
    }
  }
}

// ---------------------------------------------------------------------------
// LinkLoss (safety invariant 4: the stale-out path must be exercised on every desk)
// ---------------------------------------------------------------------------

TEST_CASE("LinkLoss: delivers frames for ten seconds, then goes silent and reports Stalled", "[mock_source]") {
  MockSource source = make(MockProfile::LinkLoss, 0);
  std::uint32_t delivered = 0;
  std::uint32_t last_t_ms = 0;
  std::uint64_t last_wall_ms = 0;
  std::optional<std::uint64_t> silent_since;
  for (std::uint64_t now = 0; now < 30'000; now += kPeriod) {
    const auto f = poll_at(source, now);
    if (f) {
      REQUIRE_FALSE(silent_since.has_value());  // once silent, never a frame again
      ++delivered;
      last_t_ms = f->t_ms;
      last_wall_ms = now;
      CHECK(source.health().state == LinkState::Live);
    } else {
      if (!silent_since) {
        silent_since = now;
      }
      CHECK(source.health().state == LinkState::Stalled);
    }
  }
  REQUIRE(silent_since.has_value());
  CHECK(delivered == 100);
  CHECK(last_t_ms == 10'000);
  CHECK(*silent_since == 10'000);
  const SourceHealth h = source.health();
  CHECK(h.state == LinkState::Stalled);
  CHECK(h.frames_ok == 100);
  CHECK(h.last_frame_monotonic_ms == last_wall_ms);
}

TEST_CASE("LinkLoss: the first ten seconds are indistinguishable from WarmupDrive", "[mock_source]") {
  MockSource loss = make(MockProfile::LinkLoss, 0);
  MockSource warm = make(MockProfile::WarmupDrive, 0);
  for (std::uint64_t now = 0; now < 10'000; now += kPeriod) {
    const Frame a = frame_at(loss, now);
    const Frame b = frame_at(warm, now);
    REQUIRE(a.t_ms == b.t_ms);
    REQUIRE(same_model_output(a, b));
  }
}

TEST_CASE("LinkLoss: stays silent no matter how long or how often it is polled afterwards", "[mock_source]") {
  MockSource source = make(MockProfile::LinkLoss, 0);
  for (std::uint64_t now = 0; now <= 10'000; now += kPeriod) {
    static_cast<void>(poll_at(source, now));
  }
  REQUIRE(source.health().state == LinkState::Stalled);
  CHECK_FALSE(poll_at(source, 10'100).has_value());
  CHECK_FALSE(poll_at(source, 20'000).has_value());
  CHECK_FALSE(poll_at(source, 600'000).has_value());
  CHECK_FALSE(poll_at(source, 600'050).has_value());
  CHECK(source.health().state == LinkState::Stalled);
  CHECK(source.health().frames_ok == 100);
}

// ---------------------------------------------------------------------------
// MockFaults: the simulated MCU's thresholds and hysteresis
// ---------------------------------------------------------------------------

[[nodiscard]] Frame healthy() noexcept {
  Frame f;
  f.oil_psi = 40.0F;
  f.coolant_f = 190.0F;
  f.fuel_fraction = 0.5F;
  f.voltage_v = 14.2F;
  return f;
}

TEST_CASE("MockFaults: a healthy frame has no faults", "[mock_source]") {
  MockFaults faults;
  Frame f = healthy();
  faults.apply(f);
  CHECK(f.faults.empty());
}

TEST_CASE("MockFaults: apply clears whatever faults the frame carried before", "[mock_source]") {
  MockFaults faults;
  Frame f = healthy();
  REQUIRE(f.faults.push_back(Fault{.kind = FaultKind::CltHigh}));
  REQUIRE(f.faults.push_back(Fault{.kind = FaultKind::SenderShort, .channel = Channel::Volt}));
  faults.apply(f);
  CHECK(f.faults.empty());
}

TEST_CASE("MockFaults: OIL_LOW sets below 12 psi and clears at 15 psi", "[mock_source]") {
  MockFaults faults;
  Frame f = healthy();

  f.oil_psi = 12.0F;
  faults.apply(f);
  CHECK_FALSE(has_fault(f, FaultKind::OilLow));  // strict: 12.0 is not below 12

  f.oil_psi = 11.9F;
  faults.apply(f);
  CHECK(has_fault(f, FaultKind::OilLow));

  f.oil_psi = 13.5F;
  faults.apply(f);
  CHECK(has_fault(f, FaultKind::OilLow));  // inside the hysteresis band: still latched

  f.oil_psi = 14.99F;
  faults.apply(f);
  CHECK(has_fault(f, FaultKind::OilLow));

  f.oil_psi = 15.0F;
  faults.apply(f);
  CHECK_FALSE(has_fault(f, FaultKind::OilLow));  // clears at or above 15

  f.oil_psi = 13.5F;
  faults.apply(f);
  CHECK_FALSE(has_fault(f, FaultKind::OilLow));  // same value, opposite history: stays clear
}

TEST_CASE("MockFaults: CLT_HIGH sets above 235 F and clears at 228 F", "[mock_source]") {
  MockFaults faults;
  Frame f = healthy();

  f.coolant_f = 235.0F;
  faults.apply(f);
  CHECK_FALSE(has_fault(f, FaultKind::CltHigh));

  f.coolant_f = 235.5F;
  faults.apply(f);
  CHECK(has_fault(f, FaultKind::CltHigh));

  f.coolant_f = 230.0F;
  faults.apply(f);
  CHECK(has_fault(f, FaultKind::CltHigh));

  f.coolant_f = 228.0F;
  faults.apply(f);
  CHECK_FALSE(has_fault(f, FaultKind::CltHigh));

  f.coolant_f = 230.0F;
  faults.apply(f);
  CHECK_FALSE(has_fault(f, FaultKind::CltHigh));
}

TEST_CASE("MockFaults: FUEL_LOW sets below 8 percent and clears at 10 percent", "[mock_source]") {
  MockFaults faults;
  Frame f = healthy();

  f.fuel_fraction = 0.08F;
  faults.apply(f);
  CHECK_FALSE(has_fault(f, FaultKind::FuelLow));

  f.fuel_fraction = 0.079F;
  faults.apply(f);
  CHECK(has_fault(f, FaultKind::FuelLow));

  f.fuel_fraction = 0.09F;
  faults.apply(f);
  CHECK(has_fault(f, FaultKind::FuelLow));

  f.fuel_fraction = 0.10F;
  faults.apply(f);
  CHECK_FALSE(has_fault(f, FaultKind::FuelLow));

  f.fuel_fraction = 0.09F;
  faults.apply(f);
  CHECK_FALSE(has_fault(f, FaultKind::FuelLow));
}

TEST_CASE("MockFaults: VOLT_LOW sets below 12.4 V and clears at 12.8 V", "[mock_source]") {
  MockFaults faults;
  Frame f = healthy();

  f.voltage_v = 12.4F;
  faults.apply(f);
  CHECK_FALSE(has_fault(f, FaultKind::VoltLow));

  f.voltage_v = 12.39F;
  faults.apply(f);
  CHECK(has_fault(f, FaultKind::VoltLow));

  f.voltage_v = 12.6F;
  faults.apply(f);
  CHECK(has_fault(f, FaultKind::VoltLow));

  f.voltage_v = 12.8F;
  faults.apply(f);
  CHECK_FALSE(has_fault(f, FaultKind::VoltLow));

  f.voltage_v = 12.6F;
  faults.apply(f);
  CHECK_FALSE(has_fault(f, FaultKind::VoltLow));
}

TEST_CASE("MockFaults: VOLT_HIGH sets above 15.2 V and clears at 14.9 V", "[mock_source]") {
  MockFaults faults;
  Frame f = healthy();

  f.voltage_v = 15.2F;
  faults.apply(f);
  CHECK_FALSE(has_fault(f, FaultKind::VoltHigh));

  f.voltage_v = 15.21F;
  faults.apply(f);
  CHECK(has_fault(f, FaultKind::VoltHigh));

  f.voltage_v = 15.0F;
  faults.apply(f);
  CHECK(has_fault(f, FaultKind::VoltHigh));

  f.voltage_v = 14.9F;
  faults.apply(f);
  CHECK_FALSE(has_fault(f, FaultKind::VoltHigh));

  f.voltage_v = 15.0F;
  faults.apply(f);
  CHECK_FALSE(has_fault(f, FaultKind::VoltHigh));
}

TEST_CASE("MockFaults: simultaneous faults are reported in a fixed order with default channels", "[mock_source]") {
  MockFaults faults;
  Frame f;
  f.oil_psi = 5.0F;
  f.coolant_f = 240.0F;
  f.fuel_fraction = 0.05F;
  f.voltage_v = 12.0F;
  faults.apply(f);
  REQUIRE(f.faults.size() == 4);
  CHECK(f.faults[0].kind == FaultKind::OilLow);
  CHECK(f.faults[1].kind == FaultKind::CltHigh);
  CHECK(f.faults[2].kind == FaultKind::FuelLow);
  CHECK(f.faults[3].kind == FaultKind::VoltLow);
  for (const auto& fault : f.faults) {
    CHECK(fault.channel == Channel{});
  }
}

TEST_CASE("MockFaults: VOLT_LOW and VOLT_HIGH hand over cleanly as voltage swings", "[mock_source]") {
  MockFaults faults;
  Frame f = healthy();
  f.voltage_v = 12.0F;
  faults.apply(f);
  CHECK(has_fault(f, FaultKind::VoltLow));
  CHECK_FALSE(has_fault(f, FaultKind::VoltHigh));

  f.voltage_v = 15.5F;
  faults.apply(f);
  CHECK_FALSE(has_fault(f, FaultKind::VoltLow));  // cleared: 15.5 is above 12.8
  CHECK(has_fault(f, FaultKind::VoltHigh));

  f.voltage_v = 14.2F;
  faults.apply(f);
  CHECK(f.faults.empty());
}

TEST_CASE("MockFaults: an open oil sender suppresses OIL_LOW and appends SENDER_OPEN_OIL last", "[mock_source]") {
  MockFaults faults;
  Frame f = healthy();
  f.oil_psi = 0.0F;
  f.coolant_f = 240.0F;
  faults.apply(f, /*oil_sender_open=*/true);
  REQUIRE(f.faults.size() == 2);
  CHECK(f.faults[0].kind == FaultKind::CltHigh);
  CHECK(f.faults[1].kind == FaultKind::SenderOpen);
  CHECK(f.faults[1].channel == Channel::Oil);
  CHECK_FALSE(has_fault(f, FaultKind::OilLow));
}

TEST_CASE("MockFaults: OIL_LOW does not stay latched through an open-sender period", "[mock_source]") {
  MockFaults faults;
  Frame f = healthy();
  f.oil_psi = 5.0F;
  faults.apply(f);
  REQUIRE(has_fault(f, FaultKind::OilLow));

  faults.apply(f, /*oil_sender_open=*/true);
  CHECK_FALSE(has_fault(f, FaultKind::OilLow));
  CHECK(has_fault(f, FaultKind::SenderOpen, Channel::Oil));

  // Sender reconnects at a healthy pressure: no stale OIL_LOW from before the open period.
  f.oil_psi = 13.0F;  // inside the hysteresis band
  faults.apply(f);
  CHECK_FALSE(has_fault(f, FaultKind::OilLow));
  CHECK_FALSE(has_fault(f, FaultKind::SenderOpen));
}

TEST_CASE("MockFaults: a healthy frame with an open sender carries only the sender fault", "[mock_source]") {
  MockFaults faults;
  Frame f = healthy();
  faults.apply(f, /*oil_sender_open=*/true);
  REQUIRE(f.faults.size() == 1);
  CHECK(f.faults[0].kind == FaultKind::SenderOpen);
  CHECK(f.faults[0].channel == Channel::Oil);
}

TEST_CASE("MockFaults: hysteresis state is per instance", "[mock_source]") {
  MockFaults a;
  MockFaults b;
  Frame f = healthy();
  f.oil_psi = 5.0F;
  a.apply(f);
  REQUIRE(has_fault(f, FaultKind::OilLow));

  f.oil_psi = 13.5F;  // in the band: `a` would hold the fault, `b` has no history
  b.apply(f);
  CHECK_FALSE(has_fault(f, FaultKind::OilLow));
  a.apply(f);
  CHECK(has_fault(f, FaultKind::OilLow));
}

}  // namespace
