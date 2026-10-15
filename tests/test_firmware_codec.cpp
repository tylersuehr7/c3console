// The MCU's codecs against the Pi's: frames the firmware writes must parse on the host, and
// commands the host formats must parse on the firmware. Both directions share one CRC and one
// set of fault-code strings, so these tests are where a drift between the two ends shows up.
#include "c3fw/command.hpp"
#include "c3fw/frame_writer.hpp"
#include "telemetry/crc16.hpp"
#include "telemetry/frame.hpp"
#include "telemetry/protocol.hpp"
#include "vehicle/vehicle_commands.hpp"
#include "vehicle/vehicle_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

using c3::telemetry::Channel;
using c3::telemetry::Crc16Ccitt;
using c3::telemetry::Fault;
using c3::telemetry::FaultKind;
using c3::telemetry::Frame;
using c3::telemetry::kMaxFaults;
using c3::telemetry::kMaxLineLength;
using c3::telemetry::parse_frame;
using c3::telemetry::ParseError;
using c3::telemetry::ParseResult;
using c3::vehicle::HvacBlower;
using c3::vehicle::HvacMode;
using c3::vehicle::HvacState;
using c3::vehicle::LampState;
using c3::vehicle::RelayState;
using c3fw::Command;
using c3fw::CommandKind;
using c3fw::CommandResult;
using c3fw::parse_command;
using c3fw::write_ack;
using c3fw::write_frame;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinAbs;

CATCH_REGISTER_ENUM(ParseError, ParseError::None, ParseError::Empty, ParseError::TooLong, ParseError::MissingCrc,
                    ParseError::BadCrcField, ParseError::CrcMismatch, ParseError::Malformed, ParseError::MissingField,
                    ParseError::OutOfRange, ParseError::UnknownFault, ParseError::TooManyFaults)
CATCH_REGISTER_ENUM(CommandKind, CommandKind::Hvac, CommandKind::Lamp, CommandKind::CalAdd, CommandKind::CalSave,
                    CommandKind::Ping)
CATCH_REGISTER_ENUM(HvacMode, HvacMode::Vent, HvacMode::Floor, HvacMode::Defrost)
CATCH_REGISTER_ENUM(HvacBlower, HvacBlower::Off, HvacBlower::L1, HvacBlower::L2, HvacBlower::L3, HvacBlower::L4)

namespace {

/// A frame with every optional feature in use.
[[nodiscard]] Frame full_frame() {
  Frame f;
  f.t_ms = 184523;
  f.fuel_fraction = 0.6234F;
  f.oil_psi = 41.25F;
  f.coolant_f = 196.4F;
  f.voltage_v = 14.125F;
  f.raw = {12843, 8211, 19004, 21877};
  REQUIRE(f.faults.push_back(Fault{.kind = FaultKind::OilLow, .channel = Channel::Fuel}));
  REQUIRE(f.faults.push_back(Fault{.kind = FaultKind::SenderOpen, .channel = Channel::Coolant}));
  f.relays = RelayState{.blower = HvacBlower::L2,
                        .mode = HvacMode::Floor,
                        .ac_enabled = true,
                        .headlights_on = true,
                        .lamp_doors_open = true,
                        .highbeams_on = false};
  f.reverse = true;
  return f;
}

/// Serialises through the firmware writer; the result includes the trailing newline.
[[nodiscard]] std::string line_of(const Frame& f) {
  std::array<char, kMaxLineLength + 1> buf{};
  const std::size_t n = write_frame(f, buf);
  REQUIRE(n > 0);
  return std::string(buf.data(), n);
}

/// Strips the newline the writer appends; the parser takes a bare line.
[[nodiscard]] std::string_view bare(std::string_view line) {
  REQUIRE_FALSE(line.empty());
  REQUIRE(line.back() == '\n');
  return line.substr(0, line.size() - 1);
}

[[nodiscard]] std::string ack_of(std::string_view cmd, bool ok, std::string_view err = {}) {
  std::array<char, 128> buf{};
  const std::size_t n = write_ack(cmd, ok, err, buf);
  REQUIRE(n > 0);
  return std::string(buf.data(), n);
}

// By value: the argument is usually a temporary, and Command::name points into the input
// literal, which outlives it.
[[nodiscard]] Command accepted(const CommandResult& r) {
  INFO("error: " << r.error);
  REQUIRE(r.command.has_value());
  return *r.command;
}

[[nodiscard]] std::string_view refused(const CommandResult& r) {
  REQUIRE_FALSE(r.command.has_value());
  return r.error;
}

}  // namespace

// ---------------------------------------------------------------------------
// write_frame
// ---------------------------------------------------------------------------

TEST_CASE("write_frame: the Pi parser accepts what the MCU writes and every field survives", "[firmware][codec]") {
  const Frame f = full_frame();
  const std::string line = line_of(f);
  const ParseResult parsed = parse_frame(bare(line));
  REQUIRE(parsed.error == ParseError::None);
  REQUIRE(parsed.frame.has_value());
  const Frame& p = *parsed.frame;

  CHECK(p.t_ms == 184523);
  CHECK_THAT(p.fuel_fraction, WithinAbs(0.6234, 1e-4));  // written with four decimals
  CHECK_THAT(p.oil_psi, WithinAbs(41.25, 1e-2));         // two
  CHECK_THAT(p.coolant_f, WithinAbs(196.4, 1e-2));       // two
  CHECK_THAT(p.voltage_v, WithinAbs(14.125, 1e-3));      // three
  CHECK(p.raw == f.raw);

  REQUIRE(p.faults.size() == 2);
  CHECK(p.faults[0].kind == FaultKind::OilLow);
  CHECK(p.faults[1].kind == FaultKind::SenderOpen);
  CHECK(p.faults[1].channel == Channel::Coolant);

  REQUIRE(p.relays.has_value());
  CHECK(p.relays->blower == HvacBlower::L2);
  CHECK(p.relays->mode == HvacMode::Floor);
  CHECK(p.relays->ac_enabled);
  CHECK(p.relays->headlights_on);
  CHECK(p.relays->lamp_doors_open == true);
  CHECK(p.relays->highbeams_on == false);
  CHECK(p.reverse);
}

TEST_CASE("write_frame: the relay object is always present; doors and high only when known", "[firmware][codec]") {
  Frame f = full_frame();
  f.relays->lamp_doors_open.reset();
  f.relays->highbeams_on.reset();
  const std::string line = line_of(f);
  CHECK_THAT(line, !ContainsSubstring("doors"));
  CHECK_THAT(line, !ContainsSubstring("high"));
  const ParseResult parsed = parse_frame(bare(line));
  REQUIRE(parsed.error == ParseError::None);
  REQUIRE(parsed.frame->relays.has_value());
  CHECK_FALSE(parsed.frame->relays->lamp_doors_open.has_value());
  CHECK_FALSE(parsed.frame->relays->highbeams_on.has_value());

  f.relays.reset();  // the MCU never does this, but the writer must still emit a valid line
  const std::string defaults = line_of(f);
  CHECK_THAT(defaults, ContainsSubstring(R"("relays":{"blower":0,"ac":false,"mode":"vent","headlights":false})"));
  CHECK(parse_frame(bare(defaults)).error == ParseError::None);
}

TEST_CASE("write_frame: the crc covers the object with its closing brace and is the last member", "[firmware][codec]") {
  const std::string line(bare(line_of(full_frame())));
  const std::size_t at = line.rfind(R"(,"crc":")");
  REQUIRE(at != std::string::npos);
  CHECK(at + 8 + 4 + 2 == line.size());  // marker, four hex digits, quote and brace
  CHECK_THAT(line, EndsWith("\"}"));

  const std::string object = line.substr(0, at) + "}";
  std::array<char, 8> hex{};
  std::snprintf(hex.data(), hex.size(), "%04x", Crc16Ccitt::compute(object));  // NOLINT
  CHECK(line.substr(at + 8, 4) == hex.data());
}

TEST_CASE("write_frame: an empty fault list and a full one both fit the line limit", "[firmware][codec]") {
  Frame f = full_frame();
  f.faults.clear();
  CHECK_THAT(line_of(f), ContainsSubstring(R"("faults":[],)"));

  // Every value fault plus three sender faults is more than the firmware can produce at once
  // (a channel with a sender fault has no value fault) but is what the list can hold.
  f.faults.clear();
  for (const FaultKind kind :
       {FaultKind::OilLow, FaultKind::CltHigh, FaultKind::FuelLow, FaultKind::VoltLow, FaultKind::VoltHigh}) {
    REQUIRE(f.faults.push_back(Fault{.kind = kind, .channel = Channel::Fuel}));
  }
  REQUIRE(f.faults.push_back(Fault{.kind = FaultKind::SenderShort, .channel = Channel::Fuel}));
  REQUIRE(f.faults.push_back(Fault{.kind = FaultKind::SenderShort, .channel = Channel::Volt}));
  REQUIRE(f.faults.push_back(Fault{.kind = FaultKind::SenderOpen, .channel = Channel::Coolant}));
  REQUIRE(f.faults.size() == kMaxFaults);
  const std::string line = line_of(f);
  CHECK(line.size() <= kMaxLineLength + 1);
  const ParseResult parsed = parse_frame(bare(line));
  REQUIRE(parsed.error == ParseError::None);
  CHECK(parsed.frame->faults.size() == kMaxFaults);
}

TEST_CASE("write_frame: a buffer that cannot hold the line yields 0, never a truncated frame", "[firmware][codec]") {
  const Frame f = full_frame();
  std::array<char, 0> none{};
  std::array<char, 64> small{};
  std::array<char, 200> almost{};
  CHECK(write_frame(f, none) == 0);
  CHECK(write_frame(f, small) == 0);
  CHECK(write_frame(f, almost) == 0);
  std::array<char, kMaxLineLength> enough{};
  CHECK(write_frame(f, enough) > 0);
}

// ---------------------------------------------------------------------------
// write_ack
// ---------------------------------------------------------------------------

TEST_CASE("write_ack: both forms match spec §4.2 and the host parser reads them", "[firmware][codec]") {
  const std::string ok = ack_of("hvac", true);
  CHECK(ok == "{\"ack\":\"hvac\",\"ok\":true}\n");
  const auto parsed_ok = c3::vehicle::parse_acknowledgement(bare(ok));
  REQUIRE(parsed_ok.has_value());
  CHECK(parsed_ok->kind == c3::vehicle::CommandKind::Hvac);
  CHECK(parsed_ok->ok);

  const std::string refused_line = ack_of("lamp", false, "lamp needs headlights");
  CHECK(refused_line == "{\"ack\":\"lamp\",\"ok\":false,\"err\":\"lamp needs headlights\"}\n");
  const auto parsed_refused = c3::vehicle::parse_acknowledgement(bare(refused_line));
  REQUIRE(parsed_refused.has_value());
  CHECK(parsed_refused->kind == c3::vehicle::CommandKind::Lamp);
  CHECK_FALSE(parsed_refused->ok);
}

TEST_CASE("write_ack: only a plain token is echoed as the command name", "[firmware][codec]") {
  CHECK_THAT(ack_of("", false, "missing cmd"), StartsWith(R"({"ack":"?",)"));
  CHECK_THAT(ack_of("a\"b", false, "unknown cmd"), StartsWith(R"({"ack":"?",)"));
  CHECK_THAT(ack_of("back\\slash", false, "unknown cmd"), StartsWith(R"({"ack":"?",)"));
  CHECK_THAT(ack_of("cal_save2", true), StartsWith(R"({"ack":"cal_save2",)"));
}

TEST_CASE("write_ack: a buffer that cannot hold the ack yields 0", "[firmware][codec]") {
  std::array<char, 16> small{};
  CHECK(write_ack("hvac", true, {}, small) == 0);
  std::array<char, 26> exact{};  // {"ack":"hvac","ok":true}\n is 25 bytes plus the terminator
  CHECK(write_ack("hvac", true, {}, exact) == 25);
}

// ---------------------------------------------------------------------------
// parse_command
// ---------------------------------------------------------------------------

TEST_CASE("parse_command: the five example lines of spec §4.2", "[firmware][codec]") {
  const Command hvac = accepted(parse_command(R"({"cmd":"hvac","blower":2,"ac":true,"mode":"vent"})"));
  CHECK(hvac.kind == CommandKind::Hvac);
  CHECK(hvac.name == "hvac");
  CHECK(hvac.blower == 2);
  CHECK(hvac.ac);
  CHECK(hvac.mode == HvacMode::Vent);

  const Command lamp = accepted(parse_command(R"({"cmd":"lamp","headlights":true,"doors":false,"high":false})"));
  CHECK(lamp.kind == CommandKind::Lamp);
  CHECK(lamp.headlights);
  CHECK(lamp.doors == false);
  CHECK(lamp.high == false);

  const Command add = accepted(parse_command(R"({"cmd":"cal","ch":"fuel","action":"add","raw":12843,"value":0.50})"));
  CHECK(add.kind == CommandKind::CalAdd);
  CHECK(add.channel == Channel::Fuel);
  CHECK(add.raw == 12843);
  CHECK_THAT(add.value, WithinAbs(0.5, 1e-6));

  const Command save = accepted(parse_command(R"({"cmd":"cal","ch":"clt","action":"save"})"));
  CHECK(save.kind == CommandKind::CalSave);
  CHECK(save.channel == Channel::Coolant);

  CHECK(accepted(parse_command(R"({"cmd":"ping"})")).kind == CommandKind::Ping);
}

TEST_CASE("parse_command: what the host's formatters emit is accepted verbatim", "[firmware][codec]") {
  std::array<char, c3::vehicle::kMaxCommandLength + 1> buf{};

  const HvacState hvac{.mode = HvacMode::Floor, .blower = HvacBlower::L3, .ac_enabled = true};
  std::size_t n = c3::vehicle::format_hvac_command(hvac, buf);
  REQUIRE(n > 0);
  const Command h = accepted(parse_command(std::string_view(buf.data(), n)));
  CHECK(h.kind == CommandKind::Hvac);
  CHECK(h.blower == 3);
  CHECK(h.ac);
  CHECK(h.mode == HvacMode::Floor);

  const LampState lamp{.lamp_doors_open = true, .headlights_on = true, .highbeams_on = false};
  n = c3::vehicle::format_lamp_command(lamp, buf);
  REQUIRE(n > 0);
  const Command l = accepted(parse_command(std::string_view(buf.data(), n)));
  CHECK(l.kind == CommandKind::Lamp);
  CHECK(l.headlights);
  CHECK(l.doors == true);
  CHECK(l.high == false);
}

TEST_CASE("parse_command: a missing or malformed field rejects the whole command and names the problem",
          "[firmware][codec]") {
  CHECK(refused(parse_command(R"({"cmd":"hvac","blower":2,"ac":true})")) == "hvac needs blower, ac and mode");
  CHECK(refused(parse_command(R"({"cmd":"hvac","blower":5,"ac":true,"mode":"vent"})")) == "blower must be 0..4");
  CHECK(refused(parse_command(R"({"cmd":"hvac","blower":"2","ac":true,"mode":"vent"})")) == "blower must be 0..4");
  CHECK(refused(parse_command(R"({"cmd":"hvac","blower":2,"ac":"yes","mode":"vent"})")) == "ac must be true|false");
  CHECK(refused(parse_command(R"({"cmd":"hvac","blower":2,"ac":true,"mode":"auto"})")) ==
        "mode must be vent|floor|defrost");
  CHECK(refused(parse_command(R"({"cmd":"lamp","doors":true})")) == "lamp needs headlights");
  CHECK(refused(parse_command(R"({"cmd":"lamp","headlights":true,"doors":1})")) == "doors must be true|false");
  CHECK(refused(parse_command(R"({"cmd":"lamp","headlights":true,"high":"on"})")) == "high must be true|false");
  CHECK(refused(parse_command(R"({"cmd":"cal","ch":"fuel"})")) == "cal needs ch and action");
  CHECK(refused(parse_command(R"({"cmd":"cal","ch":"fuel","action":"add","raw":1})")) == "cal add needs raw and value");
  CHECK(refused(parse_command(R"({"cmd":"cal","ch":"fuel","action":"clear"})")) == "cal action must be add|save");
  CHECK(refused(parse_command(R"({"cmd":"cal","ch":"water","action":"save"})")) == "ch must be fuel|oil|clt|volt");
  CHECK(refused(parse_command(R"({"cmd":"cal","ch":"oil","action":"add","raw":1.5,"value":1})")) ==
        "raw must be an integer");
  CHECK(refused(parse_command(R"({"cmd":"cal","ch":"oil","action":"add","raw":1,"value":"x"})")) ==
        "value must be a number");

  // The name is known even when the command is refused, so the ack can carry it.
  const CommandResult r = parse_command(R"({"cmd":"hvac","blower":9,"ac":true,"mode":"vent"})");
  CHECK(r.name == "hvac");
}

TEST_CASE("parse_command: A/C without the blower is refused, not quietly dropped (spec §6.1)", "[firmware][codec]") {
  CHECK(refused(parse_command(R"({"cmd":"hvac","blower":0,"ac":true,"mode":"vent"})")) == "ac needs the blower on");
  CHECK(accepted(parse_command(R"({"cmd":"hvac","blower":1,"ac":true,"mode":"vent"})")).ac);
  CHECK_FALSE(accepted(parse_command(R"({"cmd":"hvac","blower":0,"ac":false,"mode":"vent"})")).ac);
}

TEST_CASE("parse_command: optional lamp fields are absent, not false, when the Pi omits them", "[firmware][codec]") {
  const Command lamp = accepted(parse_command(R"({"cmd":"lamp","headlights":false})"));
  CHECK_FALSE(lamp.doors.has_value());
  CHECK_FALSE(lamp.high.has_value());
}

TEST_CASE("parse_command: unknown keys are ignored; unknown, foreign, missing and malformed commands are not",
          "[firmware][codec]") {
  CHECK(accepted(parse_command(R"({"cmd":"ping","extra":1,"note":"x"})")).kind == CommandKind::Ping);
  CHECK(refused(parse_command(R"({"cmd":"boost"})")) == "unknown cmd");
  CHECK(refused(parse_command(R"({"cmd":"replay","file":"drive.csv"})")) ==
        "replay is played by the display host, not the MCU");
  CHECK(refused(parse_command(R"({"cmd":5})")) == "cmd must be a string");

  const CommandResult missing = parse_command(R"({"blower":2})");
  CHECK(refused(missing) == "missing cmd");
  CHECK(missing.name.empty());

  CHECK(refused(parse_command(R"({"cmd":"ping")")) == "malformed json");
  CHECK(refused(parse_command("")) == "malformed json");
  CHECK(refused(parse_command("ping")) == "malformed json");
  CHECK(refused(parse_command(R"({"cmd":"ping"} trailing)")) == "malformed json");
}
