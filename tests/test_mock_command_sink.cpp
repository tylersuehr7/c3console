// mock_command_sink.cpp — commands against the mock source: validation, logging, round trip.
#include "app/mock_command_sink.hpp"
#include "logging/log.hpp"
#include "telemetry/frame.hpp"
#include "telemetry/mock_source.hpp"
#include "vehicle/vehicle_commands.hpp"
#include "vehicle/vehicle_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using c3::app::MockCommandSink;
using c3::telemetry::Frame;
using c3::telemetry::MockProfile;
using c3::telemetry::MockSource;
using c3::vehicle::CommandKind;
using c3::vehicle::CommandStatus;
using c3::vehicle::HvacBlower;
using c3::vehicle::HvacMode;
using c3::vehicle::HvacState;
using c3::vehicle::LampState;
using c3::vehicle::SubmitResult;
using Catch::Matchers::ContainsSubstring;
namespace l = c3::logging;

CATCH_REGISTER_ENUM(SubmitResult, SubmitResult::Accepted, SubmitResult::Busy, SubmitResult::Unavailable,
                    SubmitResult::Invalid)
CATCH_REGISTER_ENUM(CommandStatus, CommandStatus::Idle, CommandStatus::Pending, CommandStatus::Acknowledged,
                    CommandStatus::Confirmed, CommandStatus::Rejected, CommandStatus::TimedOut,
                    CommandStatus::Unavailable)

namespace {

// The mock source takes a plain function pointer for its clock, so the fake time is file-scope
// state that each test case sets before polling. Catch2 runs cases one at a time.
std::uint64_t g_now_ms = 0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

[[nodiscard]] std::uint64_t fake_now() noexcept {
  return g_now_ms;
}

/// Captures log lines and holds the level at Info so info-level command echoes are seen.
class LogCapture {
public:
  LogCapture() : previous_level_(l::get_level()), sink_(l::Sink{.context = this, .emit = &LogCapture::emit}) {
    l::set_level(l::Level::Info);
  }
  ~LogCapture() { l::set_level(previous_level_); }
  LogCapture(const LogCapture&) = delete;
  LogCapture& operator=(const LogCapture&) = delete;
  LogCapture(LogCapture&&) = delete;
  LogCapture& operator=(LogCapture&&) = delete;

  std::vector<std::string> lines;

private:
  static void emit(void* context, std::string_view line) noexcept {
    static_cast<LogCapture*>(context)->lines.emplace_back(line);
  }
  l::Level previous_level_;
  l::ScopedSink sink_;
};

/// A mock source at t = 0, the sink driving it, and a capture so command echoes stay out of
/// the test output.
struct Rig {
  Rig() : mock(MockProfile::WarmupDrive, &fake_now), sink(&mock) { g_now_ms = 0; }
  LogCapture log;
  MockSource mock;
  MockCommandSink sink;

  /// Advances the clock one period and returns the frame the mock produces for it.
  [[nodiscard]] Frame next_frame() {
    for (int attempt = 0; attempt < 3; ++attempt) {
      g_now_ms += MockSource::kPeriodMs;
      if (const auto f = mock.poll()) {
        return *f;
      }
    }
    FAIL("mock produced no frame");
    return {};
  }
};

[[nodiscard]] std::string hvac_line(const HvacState& s) {
  std::array<char, c3::vehicle::kMaxCommandLength> buf{};
  return std::string(buf.data(), c3::vehicle::format_hvac_command(s, buf));
}

[[nodiscard]] std::string lamp_line(const LampState& s) {
  std::array<char, c3::vehicle::kMaxCommandLength> buf{};
  return std::string(buf.data(), c3::vehicle::format_lamp_command(s, buf));
}

constexpr HvacState kHvacOn{.mode = HvacMode::Floor, .blower = HvacBlower::L2, .ac_enabled = true};
constexpr HvacState kHvacInvalid{.mode = HvacMode::Vent, .blower = HvacBlower::Off, .ac_enabled = true};
constexpr LampState kLampsOn{.lamp_doors_open = true, .headlights_on = true, .highbeams_on = false};
constexpr LampState kLampsInvalid{.lamp_doors_open = false, .headlights_on = false, .highbeams_on = true};

TEST_CASE("MockCommandSink: without a mock source every command is unavailable and nothing is logged",
          "[mock_command_sink]") {
  LogCapture log;
  MockCommandSink sink(nullptr);
  CHECK(sink.send_hvac(kHvacOn) == SubmitResult::Unavailable);
  CHECK(sink.send_lamp(kLampsOn) == SubmitResult::Unavailable);
  CHECK(sink.status(CommandKind::Hvac) == CommandStatus::Unavailable);
  CHECK(sink.status(CommandKind::Lamp) == CommandStatus::Unavailable);
  CHECK(log.lines.empty());
}

TEST_CASE("MockCommandSink: with a mock source commands are always Confirmed", "[mock_command_sink]") {
  const Rig rig;
  CHECK(rig.sink.status(CommandKind::Hvac) == CommandStatus::Confirmed);
  CHECK(rig.sink.status(CommandKind::Lamp) == CommandStatus::Confirmed);
}

TEST_CASE("MockCommandSink: the mock's frames start with every relay off", "[mock_command_sink]") {
  Rig rig;
  const Frame f = rig.next_frame();
  REQUIRE(f.relays.has_value());
  CHECK(f.relays->blower == HvacBlower::Off);
  CHECK(f.relays->mode == HvacMode::Vent);
  CHECK_FALSE(f.relays->ac_enabled);
  CHECK_FALSE(f.relays->headlights_on);
  CHECK(f.relays->lamp_doors_open == false);
  CHECK(f.relays->highbeams_on == false);
}

TEST_CASE("MockCommandSink: an accepted HVAC command is echoed as its UART line and re-asserted by the next frame",
          "[mock_command_sink]") {
  Rig rig;
  REQUIRE(rig.sink.send_hvac(kHvacOn) == SubmitResult::Accepted);

  REQUIRE(rig.log.lines.size() == 1);
  CHECK_THAT(rig.log.lines[0], ContainsSubstring("hvac"));
  CHECK_THAT(rig.log.lines[0], ContainsSubstring("\xE2\x86\x92 MCU " + hvac_line(kHvacOn)));

  const Frame f = rig.next_frame();
  REQUIRE(f.relays.has_value());
  CHECK(f.relays->blower == HvacBlower::L2);
  CHECK(f.relays->mode == HvacMode::Floor);
  CHECK(f.relays->ac_enabled);
  CHECK_FALSE(f.relays->headlights_on);  // untouched
}

TEST_CASE("MockCommandSink: an accepted lamp command is echoed and re-asserted", "[mock_command_sink]") {
  Rig rig;
  REQUIRE(rig.sink.send_lamp(kLampsOn) == SubmitResult::Accepted);

  REQUIRE(rig.log.lines.size() == 1);
  CHECK_THAT(rig.log.lines[0], ContainsSubstring("lamp"));
  CHECK_THAT(rig.log.lines[0], ContainsSubstring("\xE2\x86\x92 MCU " + lamp_line(kLampsOn)));

  const Frame f = rig.next_frame();
  REQUIRE(f.relays.has_value());
  CHECK(f.relays->headlights_on);
  CHECK(f.relays->lamp_doors_open == true);
  CHECK(f.relays->highbeams_on == false);
  CHECK(f.relays->blower == HvacBlower::Off);  // untouched
}

TEST_CASE("MockCommandSink: HVAC and lamp loads accumulate rather than replace each other", "[mock_command_sink]") {
  Rig rig;
  REQUIRE(rig.sink.send_hvac(kHvacOn) == SubmitResult::Accepted);
  REQUIRE(rig.sink.send_lamp(kLampsOn) == SubmitResult::Accepted);
  const Frame f = rig.next_frame();
  REQUIRE(f.relays.has_value());
  CHECK(f.relays->blower == HvacBlower::L2);
  CHECK(f.relays->ac_enabled);
  CHECK(f.relays->headlights_on);

  // turning the blower off later leaves the lamps where they were
  REQUIRE(rig.sink.send_hvac(HvacState{}) == SubmitResult::Accepted);
  const Frame g = rig.next_frame();
  REQUIRE(g.relays.has_value());
  CHECK(g.relays->blower == HvacBlower::Off);
  CHECK_FALSE(g.relays->ac_enabled);
  CHECK(g.relays->headlights_on);
}

TEST_CASE("MockCommandSink: an invalid HVAC state is refused before it is logged or applied", "[mock_command_sink]") {
  Rig rig;
  STATIC_CHECK_FALSE(c3::vehicle::is_valid(kHvacInvalid));
  CHECK(rig.sink.send_hvac(kHvacInvalid) == SubmitResult::Invalid);
  CHECK(rig.log.lines.empty());
  const Frame f = rig.next_frame();
  REQUIRE(f.relays.has_value());
  CHECK_FALSE(f.relays->ac_enabled);
  CHECK(f.relays->blower == HvacBlower::Off);
}

TEST_CASE("MockCommandSink: an invalid lamp state is refused the same way, matching UartSource",
          "[mock_command_sink]") {
  Rig rig;
  STATIC_CHECK_FALSE(c3::vehicle::is_valid(kLampsInvalid));
  CHECK(rig.sink.send_lamp(kLampsInvalid) == SubmitResult::Invalid);
  CHECK(rig.sink.send_lamp(LampState{.lamp_doors_open = false, .headlights_on = true}) == SubmitResult::Invalid);
  CHECK(rig.log.lines.empty());
  const Frame f = rig.next_frame();
  REQUIRE(f.relays.has_value());
  CHECK_FALSE(f.relays->headlights_on);
  CHECK(f.relays->highbeams_on == false);
}

TEST_CASE("MockCommandSink: a later command supersedes an earlier one of the same kind", "[mock_command_sink]") {
  Rig rig;
  REQUIRE(rig.sink.send_hvac(kHvacOn) == SubmitResult::Accepted);
  REQUIRE(rig.sink.send_hvac(HvacState{.mode = HvacMode::Defrost, .blower = HvacBlower::L4}) == SubmitResult::Accepted);
  const Frame f = rig.next_frame();
  REQUIRE(f.relays.has_value());
  CHECK(f.relays->mode == HvacMode::Defrost);
  CHECK(f.relays->blower == HvacBlower::L4);
  CHECK_FALSE(f.relays->ac_enabled);
}

TEST_CASE("MockCommandSink: the sink is a CommandSink and cannot be copied or moved", "[mock_command_sink]") {
  STATIC_CHECK(std::is_base_of_v<c3::vehicle::CommandSink, MockCommandSink>);
  STATIC_CHECK_FALSE(std::is_copy_constructible_v<MockCommandSink>);
  STATIC_CHECK_FALSE(std::is_move_constructible_v<MockCommandSink>);
  STATIC_CHECK(std::is_nothrow_constructible_v<MockCommandSink, MockSource*>);
}

}  // namespace
