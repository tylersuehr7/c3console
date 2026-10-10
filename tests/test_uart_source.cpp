#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif

#include "telemetry/crc16.hpp"
#include "telemetry/frame.hpp"
#include "telemetry/protocol.hpp"
#include "telemetry/sensor_source.hpp"
#include "telemetry/uart_source.hpp"
#include "utils/unique_fd.hpp"
#include "vehicle/vehicle_commands.hpp"
#include "vehicle/vehicle_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

using c3::telemetry::Crc16Ccitt;
using c3::telemetry::Frame;
using c3::telemetry::LinkState;
using c3::telemetry::ParseError;
using c3::telemetry::SensorSource;
using c3::telemetry::SourceHealth;
using c3::telemetry::UartSource;
using c3::utils::UniqueFd;
using c3::vehicle::CommandKind;
using c3::vehicle::CommandSink;
using c3::vehicle::CommandStatus;
using c3::vehicle::HvacBlower;
using c3::vehicle::HvacMode;
using c3::vehicle::HvacState;
using c3::vehicle::LampState;
using c3::vehicle::SubmitResult;
using Catch::Matchers::ContainsSubstring;

CATCH_REGISTER_ENUM(LinkState, LinkState::NoData, LinkState::Live, LinkState::Stalled, LinkState::Failed)
CATCH_REGISTER_ENUM(SubmitResult, SubmitResult::Accepted, SubmitResult::Busy, SubmitResult::Unavailable,
                    SubmitResult::Invalid)
CATCH_REGISTER_ENUM(CommandStatus, CommandStatus::Idle, CommandStatus::Pending, CommandStatus::Acknowledged,
                    CommandStatus::Confirmed, CommandStatus::Rejected, CommandStatus::TimedOut,
                    CommandStatus::Unavailable)
CATCH_REGISTER_ENUM(ParseError, ParseError::None, ParseError::Empty, ParseError::TooLong, ParseError::MissingCrc,
                    ParseError::BadCrcField, ParseError::CrcMismatch, ParseError::Malformed, ParseError::MissingField,
                    ParseError::OutOfRange, ParseError::UnknownFault, ParseError::TooManyFaults)

namespace {

// ---------------------------------------------------------------------------
// Fake clock and wire helpers
// ---------------------------------------------------------------------------

std::uint64_t g_now_ms = 0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
[[nodiscard]] std::uint64_t fake_now() noexcept {
  return g_now_ms;
}

constexpr std::uint32_t kTimeout = 1000;

[[nodiscard]] std::string hex4(std::uint16_t v) {
  std::array<char, 8> buf{};
  std::snprintf(buf.data(), buf.size(), "%04x", v);  // NOLINT(cppcoreguidelines-pro-type-vararg)
  return std::string(buf.data());
}

/// Seals a JSON object the way the MCU does: crc over the whole object, appended before the brace.
[[nodiscard]] std::string seal(std::string_view object) {
  REQUIRE(object.back() == '}');
  std::string line(object.substr(0, object.size() - 1));
  line += R"(,"crc":")" + hex4(Crc16Ccitt::compute(object)) + "\"}";
  return line;
}

/// A valid frame line (no newline) with optional extra members after the required ones.
[[nodiscard]] std::string frame_line(std::uint32_t t, std::string_view extra = {}) {
  std::string o = "{\"t\":" + std::to_string(t) + R"(,"fuel":0.62,"oil":41.2,"clt":196.4,"volt":14.1,"faults":[])";
  if (!extra.empty()) {
    o += ",";
    o += extra;
  }
  o += "}";
  return seal(o);
}

[[nodiscard]] std::string relays_json(HvacBlower blower, bool ac, std::string_view mode, bool headlights,
                                      std::optional<bool> doors = std::nullopt,
                                      std::optional<bool> high = std::nullopt) {
  std::string r = R"("relays":{"blower":)" + std::to_string(static_cast<int>(blower)) + R"(,"ac":)" +
                  (ac ? "true" : "false") + R"(,"mode":")" + std::string(mode) + R"(","headlights":)" +
                  (headlights ? "true" : "false");
  if (doors) {
    r += R"(,"doors":)" + std::string(*doors ? "true" : "false");
  }
  if (high) {
    r += R"(,"high":)" + std::string(*high ? "true" : "false");
  }
  return r + "}";
}

[[nodiscard]] std::string ack_line(std::string_view kind, bool ok) {
  return R"({"ack":")" + std::string(kind) + R"(","ok":)" + (ok ? "true" : "false") + "}";
}

void set_nonblocking(int fd) {
  const int flags = ::fcntl(fd, F_GETFL);
  REQUIRE(flags >= 0);
  REQUIRE(::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
}

/// A UartSource on one end of a socketpair; the test holds the other end as the "MCU".
struct Link {
  UniqueFd mcu;
  std::unique_ptr<UartSource> source;

  explicit Link(std::uint32_t timeout = kTimeout, std::uint64_t start_ms = 1'000'000) {
    g_now_ms = start_ms;
    std::array<int, 2> ends{};
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, ends.data()) == 0);
    set_nonblocking(ends[0]);
    set_nonblocking(ends[1]);
    mcu = UniqueFd(ends[0]);
    source = UartSource::adopt(UniqueFd(ends[1]), &fake_now, timeout);
    REQUIRE(source != nullptr);
  }

  /// The MCU transmits `text` verbatim.
  void transmit(std::string_view text) const {
    std::size_t sent = 0;
    while (sent < text.size()) {
      const auto n = ::write(mcu.get(), text.data() + sent, text.size() - sent);
      REQUIRE(n > 0);
      sent += static_cast<std::size_t>(n);
    }
  }

  void transmit_line(std::string_view line) const { transmit(std::string(line) + "\n"); }

  /// Everything the host has written to the MCU so far.
  [[nodiscard]] std::string received() const {
    std::string out;
    std::array<char, 512> buf{};
    for (;;) {
      const auto n = ::read(mcu.get(), buf.data(), buf.size());
      if (n <= 0) {
        break;
      }
      out.append(buf.data(), static_cast<std::size_t>(n));
    }
    return out;
  }

  [[nodiscard]] std::optional<Frame> poll_at(std::uint64_t t_ms) const {
    g_now_ms = t_ms;
    return source->poll();
  }

  [[nodiscard]] Frame frame_at(std::uint64_t t_ms) const {
    const auto f = poll_at(t_ms);
    INFO("poll at " << t_ms << " returned no frame");
    REQUIRE(f.has_value());
    return *f;
  }

  [[nodiscard]] SourceHealth health_at(std::uint64_t t_ms) const {
    g_now_ms = t_ms;
    return source->health();
  }

  /// Brings the link to Live with one good frame at `t_ms`.
  void go_live(std::uint64_t t_ms) const {
    transmit_line(frame_line(100));
    REQUIRE(frame_at(t_ms).t_ms == 100U);
    REQUIRE(source->health().state == LinkState::Live);
  }
};

// ---------------------------------------------------------------------------
// Type and initial state
// ---------------------------------------------------------------------------

TEST_CASE("UartSource: is a non-copyable SensorSource and CommandSink", "[uart_source]") {
  STATIC_CHECK(std::is_base_of_v<SensorSource, UartSource>);
  STATIC_CHECK(std::is_base_of_v<CommandSink, UartSource>);
  STATIC_CHECK_FALSE(std::is_copy_constructible_v<UartSource>);
  STATIC_CHECK_FALSE(std::is_move_constructible_v<UartSource>);
  const Link link;
  STATIC_CHECK(noexcept(link.source->poll()));
  STATIC_CHECK(noexcept(link.source->health()));
  STATIC_CHECK(noexcept(link.source->send_hvac(HvacState{})));
  STATIC_CHECK(noexcept(link.source->send_lamp(LampState{})));
}

TEST_CASE("adopt: exposes the adopted descriptor and starts with no data", "[uart_source]") {
  const Link link;
  CHECK(link.source->descriptor() >= 0);
  CHECK(link.source->fd() == link.source->descriptor());
  const SourceHealth h = link.source->health();
  CHECK(h.state == LinkState::NoData);
  CHECK(h.frames_ok == 0);
  CHECK(h.frames_bad_crc == 0);
  CHECK(h.frames_malformed == 0);
  CHECK(h.last_frame_monotonic_ms == 0);
  CHECK(link.source->lines_dropped() == 0);
  CHECK(link.source->last_error() == ParseError::None);
  CHECK(link.source->status(CommandKind::Hvac) == CommandStatus::Idle);
  CHECK(link.source->status(CommandKind::Lamp) == CommandStatus::Idle);
}

TEST_CASE("poll: an idle line yields nothing and is not a failure", "[uart_source]") {
  const Link link;
  for (int i = 0; i < 5; ++i) {
    CHECK_FALSE(link.poll_at(1'000'000 + static_cast<std::uint64_t>(i) * 100).has_value());
  }
  CHECK(link.source->health().state == LinkState::NoData);
}

// ---------------------------------------------------------------------------
// Receiving frames
// ---------------------------------------------------------------------------

TEST_CASE("poll: a complete frame line is parsed and the link goes Live", "[uart_source]") {
  const Link link;
  link.transmit_line(frame_line(184523));
  const Frame f = link.frame_at(1'000'000);
  CHECK(f.t_ms == 184523U);
  const SourceHealth h = link.source->health();
  CHECK(h.state == LinkState::Live);
  CHECK(h.frames_ok == 1);
  CHECK(h.last_frame_monotonic_ms == 1'000'000);
  CHECK(h.frames_bad_crc == 0);
  CHECK(h.frames_malformed == 0);
}

TEST_CASE("poll: a frame split across reads is reassembled", "[uart_source]") {
  const Link link;
  const std::string line = frame_line(7);
  link.transmit(line.substr(0, 20));
  CHECK_FALSE(link.poll_at(1'000'000).has_value());
  link.transmit(line.substr(20, 30));
  CHECK_FALSE(link.poll_at(1'000'010).has_value());
  link.transmit(line.substr(50));
  CHECK_FALSE(link.poll_at(1'000'020).has_value());  // still no newline
  link.transmit("\n");
  CHECK(link.frame_at(1'000'030).t_ms == 7U);
  CHECK(link.source->health().last_frame_monotonic_ms == 1'000'030);
}

TEST_CASE("poll: byte-at-a-time delivery still produces exactly one frame", "[uart_source]") {
  const Link link;
  const std::string stream = frame_line(9) + "\n";
  std::size_t frames = 0;
  for (std::size_t i = 0; i < stream.size(); ++i) {
    link.transmit(stream.substr(i, 1));
    if (link.poll_at(1'000'000 + i)) {
      ++frames;
    }
  }
  CHECK(frames == 1);
  CHECK(link.source->health().frames_ok == 1);
}

TEST_CASE("poll: CRLF line endings are accepted", "[uart_source]") {
  const Link link;
  link.transmit(frame_line(11) + "\r\n");
  CHECK(link.frame_at(1'000'000).t_ms == 11U);
  CHECK(link.source->health().frames_malformed == 0);
}

TEST_CASE("poll: several frames in one burst come out one per poll, in order", "[uart_source]") {
  const Link link;
  std::string burst;
  for (std::uint32_t t = 100; t <= 500; t += 100) {
    burst += frame_line(t) + "\n";
  }
  link.transmit(burst);
  for (std::uint32_t t = 100; t <= 500; t += 100) {
    CHECK(link.frame_at(1'000'000 + t).t_ms == t);
  }
  CHECK_FALSE(link.poll_at(1'001'000).has_value());
  CHECK(link.source->health().frames_ok == 5);
}

TEST_CASE("poll: blank lines between frames are neither frames nor errors", "[uart_source]") {
  const Link link;
  link.transmit("\n\r\n" + frame_line(1) + "\n\n\n" + frame_line(2) + "\n");
  CHECK(link.frame_at(1'000'000).t_ms == 1U);
  CHECK(link.frame_at(1'000'100).t_ms == 2U);
  CHECK_FALSE(link.poll_at(1'000'200).has_value());
  const SourceHealth h = link.source->health();
  CHECK(h.frames_ok == 2);
  CHECK(h.frames_malformed == 0);
  CHECK(h.frames_bad_crc == 0);
  CHECK(link.source->last_error() == ParseError::None);
}

TEST_CASE("poll: the poll timestamp, not the MCU timestamp, is recorded as arrival time", "[uart_source]") {
  const Link link;
  link.transmit_line(frame_line(999'999));
  static_cast<void>(link.frame_at(5'000));
  CHECK(link.source->health().last_frame_monotonic_ms == 5'000);
}

// ---------------------------------------------------------------------------
// Rejected lines (acceptance: CRC failures counted and dropped, never parsed)
// ---------------------------------------------------------------------------

TEST_CASE("poll: a bad CRC is counted separately and never rendered", "[uart_source]") {
  const Link link;
  const std::string good = frame_line(100);
  std::string corrupt = good;
  corrupt[5] = corrupt[5] == '1' ? '2' : '1';  // flip a digit of t
  link.transmit_line(corrupt);
  CHECK_FALSE(link.poll_at(1'000'000).has_value());
  const SourceHealth h = link.source->health();
  CHECK(h.frames_bad_crc == 1);
  CHECK(h.frames_malformed == 0);
  CHECK(h.frames_ok == 0);
  CHECK(h.state == LinkState::NoData);  // a corrupt frame does not make the link live
  CHECK(link.source->last_error() == ParseError::CrcMismatch);
}

TEST_CASE("poll: malformed lines are counted as malformed with the last reason kept", "[uart_source]") {
  const Link link;
  struct Case {
    std::string line;
    ParseError expected;
  };
  const Case cases[] = {
      {"garbage", ParseError::MissingCrc},
      {seal(R"({"t":1,"fuel":0.62,"oil":41.2,"clt":196.4,"volt":14.1})"), ParseError::MissingField},
      {seal(R"({"t":1,"fuel":2.0,"oil":41.2,"clt":196.4,"volt":14.1,"faults":[]})"), ParseError::OutOfRange},
      {seal(R"({"t":1,"fuel":0.62,"oil":41.2,"clt":196.4,"volt":14.1,"faults":["NOPE"]})"), ParseError::UnknownFault},
      {seal(R"({"t":1 "fuel":0.62,"oil":41.2,"clt":196.4,"volt":14.1,"faults":[]})"), ParseError::Malformed},
      {frame_line(1) + "x", ParseError::BadCrcField},
  };
  std::uint32_t expected_malformed = 0;
  for (const auto& c : cases) {
    INFO("line: " << c.line);
    link.transmit_line(c.line);
    CHECK_FALSE(link.poll_at(1'000'000).has_value());
    ++expected_malformed;
    CHECK(link.source->health().frames_malformed == expected_malformed);
    CHECK(link.source->last_error() == c.expected);
  }
  CHECK(link.source->health().frames_bad_crc == 0);
  CHECK(link.source->health().frames_ok == 0);
}

TEST_CASE("poll: a good frame after rejected lines is delivered and the counters stand", "[uart_source]") {
  const Link link;
  link.transmit("garbage\n" + frame_line(1) + "\n");
  CHECK(link.frame_at(1'000'000).t_ms == 1U);
  const SourceHealth h = link.source->health();
  CHECK(h.frames_ok == 1);
  CHECK(h.frames_malformed == 1);
  CHECK(h.state == LinkState::Live);
  CHECK(link.source->last_error() == ParseError::MissingCrc);  // a success does not erase the last reason
}

TEST_CASE("poll: binary garbage mid-stream costs only the line it landed in", "[uart_source]") {
  const Link link;
  const std::string noise("\x00\xff\x7f\x01\x80 noise", 11);
  link.transmit(frame_line(1) + "\n" + noise + "\n" + frame_line(2) + "\n");
  CHECK(link.frame_at(1'000'000).t_ms == 1U);
  CHECK(link.frame_at(1'000'100).t_ms == 2U);
  CHECK(link.source->health().frames_malformed == 1);
  CHECK(link.source->health().frames_ok == 2);
}

TEST_CASE("poll: an overlong line is dropped by the reader, not counted as malformed", "[uart_source]") {
  const Link link;
  const std::string burst(c3::telemetry::kMaxLineLength + 100, 'g');
  link.transmit(burst + "\n" + frame_line(3) + "\n");
  std::optional<Frame> f;
  for (int i = 0; i < 10 && !f; ++i) {
    f = link.poll_at(1'000'000 + static_cast<std::uint64_t>(i));
  }
  REQUIRE(f.has_value());
  CHECK(f->t_ms == 3U);
  CHECK(link.source->lines_dropped() == 1);
  CHECK(link.source->health().frames_malformed == 0);
}

TEST_CASE("poll: a long garbage burst is consumed within the per-poll byte budget over several polls",
          "[uart_source]") {
  const Link link;
  const std::string burst(3000, 'g');
  link.transmit(burst + "\n" + frame_line(4) + "\n");
  CHECK_FALSE(link.poll_at(1'000'000).has_value());  // 1024 bytes of budget cannot reach the frame
  std::optional<Frame> f;
  int polls = 1;
  for (; polls < 10 && !f; ++polls) {
    f = link.poll_at(1'000'000 + static_cast<std::uint64_t>(polls));
  }
  REQUIRE(f.has_value());
  CHECK(f->t_ms == 4U);
  CHECK(polls <= 5);
  CHECK(link.source->lines_dropped() == 1);
}

TEST_CASE("poll: an acknowledgement line with nothing pending is ignored, not counted", "[uart_source]") {
  const Link link;
  link.transmit(ack_line("hvac", true) + "\n" + frame_line(1) + "\n");
  CHECK(link.frame_at(1'000'000).t_ms == 1U);
  const SourceHealth h = link.source->health();
  CHECK(h.frames_ok == 1);
  CHECK(h.frames_malformed == 0);
  CHECK(link.source->status(CommandKind::Hvac) == CommandStatus::Idle);
}

// ---------------------------------------------------------------------------
// Link state over time
// ---------------------------------------------------------------------------

TEST_CASE("health: Live within the stall timeout, Stalled at and after it", "[uart_source]") {
  const Link link(kTimeout);
  link.go_live(1'000'000);
  CHECK(link.health_at(1'000'999).state == LinkState::Live);
  CHECK(link.health_at(1'001'000).state == LinkState::Stalled);
  CHECK(link.health_at(1'010'000).state == LinkState::Stalled);
  CHECK(link.health_at(1'010'000).frames_ok == 1);
}

TEST_CASE("health: a new frame revives a stalled link", "[uart_source]") {
  const Link link;
  link.go_live(1'000'000);
  REQUIRE(link.health_at(1'005'000).state == LinkState::Stalled);
  link.transmit_line(frame_line(200));
  CHECK(link.frame_at(1'005'000).t_ms == 200U);
  CHECK(link.source->health().state == LinkState::Live);
  CHECK(link.source->health().last_frame_monotonic_ms == 1'005'000);
}

TEST_CASE("health: the stall timeout is honoured and capped at one second", "[uart_source]") {
  const Link quick(200);
  quick.go_live(1'000'000);
  CHECK(quick.health_at(1'000'199).state == LinkState::Live);
  CHECK(quick.health_at(1'000'200).state == LinkState::Stalled);

  const Link slow(5000);  // invariant 4: a frozen needle may never hide behind a long timeout
  slow.go_live(1'000'000);
  CHECK(slow.health_at(1'000'999).state == LinkState::Live);
  CHECK(slow.health_at(1'001'000).state == LinkState::Stalled);
}

TEST_CASE("health: a clock that steps backwards reads as Stalled rather than Live", "[uart_source]") {
  const Link link;
  link.go_live(1'000'000);
  CHECK(link.health_at(999'000).state == LinkState::Stalled);
}

TEST_CASE("health: when the device goes away the link is Failed and stays Failed", "[uart_source]") {
  Link link;
  link.go_live(1'000'000);
  link.mcu.reset();  // peer hangs up: read returns 0
  CHECK_FALSE(link.poll_at(1'000'100).has_value());
  CHECK(link.source->health().state == LinkState::Failed);
  CHECK_FALSE(link.poll_at(1'000'200).has_value());
  CHECK(link.health_at(1'000'000).state == LinkState::Failed);  // even at an instant that would be Live
  CHECK(link.source->health().frames_ok == 1);
}

TEST_CASE("health: frames already in flight are delivered before the hang-up is noticed", "[uart_source]") {
  Link link;
  link.transmit(frame_line(1) + "\n" + frame_line(2) + "\n");
  link.mcu.reset();
  CHECK(link.frame_at(1'000'000).t_ms == 1U);
  CHECK(link.frame_at(1'000'100).t_ms == 2U);
  CHECK(link.source->health().state == LinkState::Live);
  CHECK_FALSE(link.poll_at(1'000'200).has_value());
  CHECK(link.source->health().state == LinkState::Failed);
}

// ---------------------------------------------------------------------------
// Commands: submission gates
// ---------------------------------------------------------------------------

constexpr HvacState kHvacRequest{.mode = HvacMode::Vent, .blower = HvacBlower::L2, .ac_enabled = true};
constexpr LampState kLampRequest{.lamp_doors_open = true, .headlights_on = true, .highbeams_on = false};

TEST_CASE("send: commands are Unavailable until the link is Live", "[uart_source]") {
  const Link link;
  CHECK(link.source->send_hvac(kHvacRequest) == SubmitResult::Unavailable);
  CHECK(link.source->send_lamp(kLampRequest) == SubmitResult::Unavailable);
  CHECK(link.source->status(CommandKind::Hvac) == CommandStatus::Idle);
  CHECK(link.source->status(CommandKind::Lamp) == CommandStatus::Idle);
  CHECK_FALSE(link.poll_at(1'000'000).has_value());
  CHECK(link.received().empty());  // nothing was written
}

TEST_CASE("send: commands are Unavailable while the link is Stalled", "[uart_source]") {
  const Link link;
  link.go_live(1'000'000);
  g_now_ms = 1'005'000;
  REQUIRE(link.source->health().state == LinkState::Stalled);
  CHECK(link.source->send_hvac(kHvacRequest) == SubmitResult::Unavailable);
  CHECK(link.source->send_lamp(kLampRequest) == SubmitResult::Unavailable);
}

TEST_CASE("send_hvac: an HVAC state that breaks the invariants is Invalid, whatever the link state", "[uart_source]") {
  const Link link;
  const HvacState ac_without_blower{.blower = HvacBlower::Off, .ac_enabled = true};
  CHECK(link.source->send_hvac(ac_without_blower) == SubmitResult::Invalid);
  link.go_live(1'000'000);
  CHECK(link.source->send_hvac(ac_without_blower) == SubmitResult::Invalid);
  CHECK(link.source->status(CommandKind::Hvac) == CommandStatus::Idle);
  CHECK_FALSE(link.poll_at(1'000'001).has_value());
  CHECK(link.received().empty());
}

TEST_CASE("send_lamp: a lamp state that breaks the invariants is Invalid", "[uart_source]") {
  const Link link;
  link.go_live(1'000'000);
  CHECK(link.source->send_lamp(LampState{.lamp_doors_open = false, .headlights_on = true}) == SubmitResult::Invalid);
  CHECK(link.source->send_lamp(LampState{.lamp_doors_open = true, .headlights_on = false, .highbeams_on = true}) ==
        SubmitResult::Invalid);
  CHECK(link.source->status(CommandKind::Lamp) == CommandStatus::Idle);
  CHECK_FALSE(link.poll_at(1'000'001).has_value());
  CHECK(link.received().empty());
}

// ---------------------------------------------------------------------------
// Commands: the happy path
// ---------------------------------------------------------------------------

TEST_CASE("send_hvac: an accepted command is written on the next poll as one JSON line", "[uart_source]") {
  const Link link;
  link.go_live(1'000'000);
  CHECK(link.source->send_hvac(kHvacRequest) == SubmitResult::Accepted);
  CHECK(link.source->status(CommandKind::Hvac) == CommandStatus::Pending);
  CHECK(link.received().empty());  // nothing on the wire until the source is serviced
  CHECK_FALSE(link.poll_at(1'000'010).has_value());
  CHECK(link.received() == R"({"cmd":"hvac","blower":2,"ac":true,"mode":"vent"})"
                           "\n");
  CHECK_FALSE(link.poll_at(1'000'020).has_value());
  CHECK(link.received().empty());  // written exactly once
}

TEST_CASE("send_lamp: an accepted command is written as the lamp JSON line", "[uart_source]") {
  const Link link;
  link.go_live(1'000'000);
  CHECK(link.source->send_lamp(kLampRequest) == SubmitResult::Accepted);
  CHECK(link.source->status(CommandKind::Lamp) == CommandStatus::Pending);
  CHECK_FALSE(link.poll_at(1'000'010).has_value());
  CHECK(link.received() == R"({"cmd":"lamp","headlights":true,"doors":true,"high":false})"
                           "\n");
}

TEST_CASE("send: a second command of either kind is Busy while one is pending", "[uart_source]") {
  const Link link;
  link.go_live(1'000'000);
  REQUIRE(link.source->send_hvac(kHvacRequest) == SubmitResult::Accepted);
  CHECK(link.source->send_hvac(kHvacRequest) == SubmitResult::Busy);
  CHECK(link.source->send_lamp(kLampRequest) == SubmitResult::Busy);
  CHECK(link.source->status(CommandKind::Lamp) == CommandStatus::Idle);
}

TEST_CASE("command: ack then matching relay state confirms, and the sink frees up", "[uart_source]") {
  const Link link;
  link.go_live(1'000'000);
  REQUIRE(link.source->send_hvac(kHvacRequest) == SubmitResult::Accepted);
  static_cast<void>(link.poll_at(1'000'010));  // transmit

  link.transmit_line(ack_line("hvac", true));
  CHECK_FALSE(link.poll_at(1'000'050).has_value());
  CHECK(link.source->status(CommandKind::Hvac) == CommandStatus::Acknowledged);
  CHECK(link.source->send_hvac(kHvacRequest) == SubmitResult::Busy);  // still pending until state confirms

  link.transmit_line(frame_line(200, relays_json(HvacBlower::L2, true, "vent", false)));
  const Frame f = link.frame_at(1'000'100);
  CHECK(f.t_ms == 200U);
  CHECK(link.source->status(CommandKind::Hvac) == CommandStatus::Confirmed);
  CHECK(link.source->send_hvac(kHvacRequest) == SubmitResult::Accepted);  // free again
  CHECK(link.source->status(CommandKind::Hvac) == CommandStatus::Pending);
}

TEST_CASE("command: relay state that does not match keeps the command waiting", "[uart_source]") {
  const Link link;
  link.go_live(1'000'000);
  REQUIRE(link.source->send_hvac(kHvacRequest) == SubmitResult::Accepted);
  static_cast<void>(link.poll_at(1'000'010));
  link.transmit_line(ack_line("hvac", true));
  static_cast<void>(link.poll_at(1'000'020));
  REQUIRE(link.source->status(CommandKind::Hvac) == CommandStatus::Acknowledged);

  link.transmit_line(frame_line(200, relays_json(HvacBlower::L1, true, "vent", false)));  // old blower level
  static_cast<void>(link.frame_at(1'000'100));
  CHECK(link.source->status(CommandKind::Hvac) == CommandStatus::Acknowledged);

  link.transmit_line(frame_line(300, relays_json(HvacBlower::L2, false, "vent", false)));  // ac not yet on
  static_cast<void>(link.frame_at(1'000'200));
  CHECK(link.source->status(CommandKind::Hvac) == CommandStatus::Acknowledged);

  link.transmit_line(frame_line(400, relays_json(HvacBlower::L2, true, "floor", false)));  // wrong mode
  static_cast<void>(link.frame_at(1'000'300));
  CHECK(link.source->status(CommandKind::Hvac) == CommandStatus::Acknowledged);

  link.transmit_line(frame_line(500, relays_json(HvacBlower::L2, true, "vent", true)));  // headlights irrelevant
  static_cast<void>(link.frame_at(1'000'400));
  CHECK(link.source->status(CommandKind::Hvac) == CommandStatus::Confirmed);
}

TEST_CASE("command: matching relays before the ack do not confirm; the ack must come first", "[uart_source]") {
  const Link link;
  link.go_live(1'000'000);
  REQUIRE(link.source->send_hvac(kHvacRequest) == SubmitResult::Accepted);
  static_cast<void>(link.poll_at(1'000'010));

  link.transmit_line(frame_line(200, relays_json(HvacBlower::L2, true, "vent", false)));
  static_cast<void>(link.frame_at(1'000'100));
  CHECK(link.source->status(CommandKind::Hvac) == CommandStatus::Pending);

  link.transmit_line(ack_line("hvac", true));
  static_cast<void>(link.poll_at(1'000'150));
  CHECK(link.source->status(CommandKind::Hvac) == CommandStatus::Acknowledged);

  link.transmit_line(frame_line(300, relays_json(HvacBlower::L2, true, "vent", false)));
  static_cast<void>(link.frame_at(1'000'200));
  CHECK(link.source->status(CommandKind::Hvac) == CommandStatus::Confirmed);
}

TEST_CASE("command: a negative ack is Rejected and the sink frees up immediately", "[uart_source]") {
  const Link link;
  link.go_live(1'000'000);
  REQUIRE(link.source->send_hvac(kHvacRequest) == SubmitResult::Accepted);
  static_cast<void>(link.poll_at(1'000'010));
  link.transmit_line(ack_line("hvac", false));
  static_cast<void>(link.poll_at(1'000'020));
  CHECK(link.source->status(CommandKind::Hvac) == CommandStatus::Rejected);
  CHECK(link.source->send_lamp(kLampRequest) == SubmitResult::Accepted);
  CHECK(link.source->status(CommandKind::Hvac) == CommandStatus::Rejected);  // the HVAC verdict is kept
  CHECK(link.source->status(CommandKind::Lamp) == CommandStatus::Pending);
}

TEST_CASE("command: an ack for the other kind is ignored", "[uart_source]") {
  const Link link;
  link.go_live(1'000'000);
  REQUIRE(link.source->send_hvac(kHvacRequest) == SubmitResult::Accepted);
  static_cast<void>(link.poll_at(1'000'010));
  link.transmit_line(ack_line("lamp", true));
  static_cast<void>(link.poll_at(1'000'020));
  CHECK(link.source->status(CommandKind::Hvac) == CommandStatus::Pending);
  CHECK(link.source->status(CommandKind::Lamp) == CommandStatus::Idle);
  link.transmit_line(ack_line("hvac", true));
  static_cast<void>(link.poll_at(1'000'030));
  CHECK(link.source->status(CommandKind::Hvac) == CommandStatus::Acknowledged);
}

TEST_CASE("command: lamp confirmation needs headlights and doors to match", "[uart_source]") {
  const Link link;
  link.go_live(1'000'000);
  REQUIRE(link.source->send_lamp(kLampRequest) == SubmitResult::Accepted);
  static_cast<void>(link.poll_at(1'000'010));
  link.transmit_line(ack_line("lamp", true));
  static_cast<void>(link.poll_at(1'000'020));
  REQUIRE(link.source->status(CommandKind::Lamp) == CommandStatus::Acknowledged);

  link.transmit_line(frame_line(200, relays_json(HvacBlower::Off, false, "vent", true, false)));  // doors still shut
  static_cast<void>(link.frame_at(1'000'100));
  CHECK(link.source->status(CommandKind::Lamp) == CommandStatus::Acknowledged);

  link.transmit_line(frame_line(300, relays_json(HvacBlower::Off, false, "vent", true, true)));
  static_cast<void>(link.frame_at(1'000'200));
  CHECK(link.source->status(CommandKind::Lamp) == CommandStatus::Confirmed);
}

TEST_CASE("command: firmware that does not report doors can never confirm a lamp command", "[uart_source]") {
  // The host treats an absent door relay as unknown and will not infer it (spec §4.1).
  const Link link;
  link.go_live(1'000'000);
  REQUIRE(link.source->send_lamp(kLampRequest) == SubmitResult::Accepted);
  static_cast<void>(link.poll_at(1'000'010));
  link.transmit_line(ack_line("lamp", true));
  static_cast<void>(link.poll_at(1'000'020));
  link.transmit_line(frame_line(200, relays_json(HvacBlower::Off, false, "vent", true)));  // no "doors"
  static_cast<void>(link.frame_at(1'000'100));
  CHECK(link.source->status(CommandKind::Lamp) == CommandStatus::Acknowledged);
}

TEST_CASE("command: frames keep flowing while a command is pending", "[uart_source]") {
  const Link link;
  link.go_live(1'000'000);
  REQUIRE(link.source->send_hvac(kHvacRequest) == SubmitResult::Accepted);
  for (std::uint32_t t = 200; t <= 600; t += 100) {
    link.transmit_line(frame_line(t));
    CHECK(link.frame_at(1'000'000 + t).t_ms == t);
  }
  CHECK(link.source->status(CommandKind::Hvac) == CommandStatus::Pending);
  CHECK(link.source->health().frames_ok == 6);
}

TEST_CASE("command: statuses are tracked per kind", "[uart_source]") {
  const Link link;
  link.go_live(1'000'000);
  REQUIRE(link.source->send_hvac(kHvacRequest) == SubmitResult::Accepted);
  static_cast<void>(link.poll_at(1'000'010));
  link.transmit_line(ack_line("hvac", true));
  link.transmit_line(frame_line(200, relays_json(HvacBlower::L2, true, "vent", false)));
  static_cast<void>(link.frame_at(1'000'020));  // one poll consumes the ack and then the frame
  REQUIRE(link.source->status(CommandKind::Hvac) == CommandStatus::Confirmed);

  REQUIRE(link.source->send_lamp(kLampRequest) == SubmitResult::Accepted);
  CHECK(link.source->status(CommandKind::Hvac) == CommandStatus::Confirmed);
  CHECK(link.source->status(CommandKind::Lamp) == CommandStatus::Pending);
}

// ---------------------------------------------------------------------------
// Commands: timeouts, quarantine and failure
// ---------------------------------------------------------------------------

TEST_CASE("command: no acknowledgement within one second is TimedOut", "[uart_source]") {
  const Link link;
  link.go_live(1'000'000);
  REQUIRE(link.source->send_hvac(kHvacRequest) == SubmitResult::Accepted);
  link.transmit_line(frame_line(200));  // keep the link live throughout
  static_cast<void>(link.frame_at(1'000'500));
  CHECK(link.source->status(CommandKind::Hvac) == CommandStatus::Pending);
  link.transmit_line(frame_line(300));
  static_cast<void>(link.frame_at(1'000'999));
  CHECK(link.source->status(CommandKind::Hvac) == CommandStatus::Pending);
  link.transmit_line(frame_line(400));
  static_cast<void>(link.frame_at(1'001'000));
  CHECK(link.source->status(CommandKind::Hvac) == CommandStatus::TimedOut);
}

TEST_CASE("command: acknowledged but never confirmed also times out", "[uart_source]") {
  const Link link;
  link.go_live(1'000'000);
  REQUIRE(link.source->send_lamp(kLampRequest) == SubmitResult::Accepted);
  static_cast<void>(link.poll_at(1'000'010));
  link.transmit_line(ack_line("lamp", true));
  static_cast<void>(link.poll_at(1'000'020));
  REQUIRE(link.source->status(CommandKind::Lamp) == CommandStatus::Acknowledged);
  link.transmit_line(frame_line(200));
  static_cast<void>(link.frame_at(1'001'000));
  CHECK(link.source->status(CommandKind::Lamp) == CommandStatus::TimedOut);
}

TEST_CASE("command: after a timeout the sink is quarantined so a late ack cannot confirm a later request",
          "[uart_source]") {
  const Link link;
  link.go_live(1'000'000);
  REQUIRE(link.source->send_hvac(kHvacRequest) == SubmitResult::Accepted);
  link.transmit_line(frame_line(200));
  static_cast<void>(link.frame_at(1'001'000));
  REQUIRE(link.source->status(CommandKind::Hvac) == CommandStatus::TimedOut);
  REQUIRE(link.source->health().state == LinkState::Live);

  CHECK(link.source->send_hvac(kHvacRequest) == SubmitResult::Unavailable);
  CHECK(link.source->send_lamp(kLampRequest) == SubmitResult::Unavailable);

  // The late ack arrives: it must change nothing.
  link.transmit_line(ack_line("hvac", true));
  static_cast<void>(link.poll_at(1'001'010));
  CHECK(link.source->status(CommandKind::Hvac) == CommandStatus::TimedOut);
  CHECK(link.source->send_hvac(kHvacRequest) == SubmitResult::Unavailable);
}

TEST_CASE("command: a timed-out command that was never written stays off the wire", "[uart_source]") {
  const Link link;
  link.go_live(1'000'000);
  REQUIRE(link.source->send_hvac(kHvacRequest) == SubmitResult::Accepted);
  // No poll until well after the deadline: service_command times out before it transmits.
  static_cast<void>(link.poll_at(1'002'000));
  CHECK(link.source->status(CommandKind::Hvac) == CommandStatus::TimedOut);
  CHECK(link.received().empty());
}

TEST_CASE("command: a device failure while pending reports Unavailable", "[uart_source]") {
  Link link;
  link.go_live(1'000'000);
  REQUIRE(link.source->send_hvac(kHvacRequest) == SubmitResult::Accepted);
  static_cast<void>(link.poll_at(1'000'010));
  link.mcu.reset();
  static_cast<void>(link.poll_at(1'000'020));  // notices the hang-up
  REQUIRE(link.source->health().state == LinkState::Failed);
  static_cast<void>(link.poll_at(1'000'030));  // services the pending command against a failed link
  CHECK(link.source->status(CommandKind::Hvac) == CommandStatus::Unavailable);
  CHECK(link.source->send_lamp(kLampRequest) == SubmitResult::Unavailable);
}

// ---------------------------------------------------------------------------
// open(): the real device path, exercised through a pseudo-terminal
// ---------------------------------------------------------------------------

TEST_CASE("open: an unsupported baud rate is refused before touching the device", "[uart_source]") {
  g_now_ms = 0;
  const auto result = UartSource::open({.device = "/nonexistent/tty", .baud = 1234}, &fake_now);
  CHECK(result.source == nullptr);
  CHECK(result.error == "unsupported baud rate 1234");
}

TEST_CASE("open: a missing device reports the path and the system reason", "[uart_source]") {
  g_now_ms = 0;
  const auto result = UartSource::open({.device = "/nonexistent-c3console/ttyAMA0"}, &fake_now);
  CHECK(result.source == nullptr);
  CHECK_THAT(result.error, ContainsSubstring("/nonexistent-c3console/ttyAMA0: "));
  CHECK(result.error.size() > std::string_view("/nonexistent-c3console/ttyAMA0: ").size());
}

TEST_CASE("open: configures a serial device and exchanges frames and commands through it", "[uart_source]") {
  int master = -1;
  int slave = -1;
  std::array<char, 128> name{};
  REQUIRE(::openpty(&master, &slave, name.data(), nullptr, nullptr) == 0);
  const UniqueFd mcu(master);
  static_cast<void>(::close(slave));  // open() will reopen the slave by name
  set_nonblocking(master);

  g_now_ms = 1'000'000;
  auto result = UartSource::open({.device = name.data(), .baud = 115200, .stall_timeout_ms = kTimeout}, &fake_now);
  INFO("error: " << result.error);
  REQUIRE(result.source != nullptr);
  CHECK(result.error.empty());
  CHECK(result.source->descriptor() >= 0);
  CHECK(result.source->health().state == LinkState::NoData);

  // MCU -> host: a frame through the raw-mode tty.
  const std::string line = frame_line(42) + "\n";
  REQUIRE(::write(master, line.data(), line.size()) == static_cast<ssize_t>(line.size()));
  std::optional<Frame> f;
  for (int i = 0; i < 50 && !f; ++i) {
    g_now_ms = 1'000'000 + static_cast<std::uint64_t>(i);
    f = result.source->poll();
    if (!f) {
      ::usleep(2000);  // the pty line discipline may deliver asynchronously
    }
  }
  REQUIRE(f.has_value());
  CHECK(f->t_ms == 42U);
  CHECK(result.source->health().state == LinkState::Live);

  // Host -> MCU: a command comes out of the master side as one line.
  REQUIRE(result.source->send_hvac(kHvacRequest) == SubmitResult::Accepted);
  g_now_ms = 1'000'100;
  static_cast<void>(result.source->poll());
  std::string received;
  for (int i = 0; i < 50 && received.find('\n') == std::string::npos; ++i) {
    std::array<char, 128> buf{};
    const auto n = ::read(master, buf.data(), buf.size());
    if (n > 0) {
      received.append(buf.data(), static_cast<std::size_t>(n));
    } else {
      ::usleep(2000);
    }
  }
  CHECK(received == R"({"cmd":"hvac","blower":2,"ac":true,"mode":"vent"})"
                    "\n");
}

}  // namespace
