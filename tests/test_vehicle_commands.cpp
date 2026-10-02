#include "utils/flat_json.hpp"
#include "vehicle/vehicle_commands.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace {

using CommandBuffer = std::array<char, c3::vehicle::kMaxCommandLength>;

/// Formats into a full-size buffer and returns the written text as a view.
[[nodiscard]] std::string_view format(const c3::vehicle::HvacState& state, CommandBuffer& buffer) noexcept {
  const auto length = c3::vehicle::format_hvac_command(state, buffer);
  return {buffer.data(), length};
}

[[nodiscard]] std::string_view format(const c3::vehicle::LampState& state, CommandBuffer& buffer) noexcept {
  const auto length = c3::vehicle::format_lamp_command(state, buffer);
  return {buffer.data(), length};
}

// ---------------------------------------------------------------------------
// format_hvac_command
// ---------------------------------------------------------------------------

TEST_CASE("format_hvac_command: test that HvacState returns expected JSON line commands", "[vehicle_commands]") {
  CommandBuffer buffer{};
  const auto hvac = c3::vehicle::HvacState{
      .mode = c3::vehicle::HvacMode::Vent,
      .blower = c3::vehicle::HvacBlower::L2,
      .ac_enabled = true,
  };
  CHECK(format(hvac, buffer) == R"({"cmd":"hvac","blower":2,"ac":true,"mode":"vent"})");
}

TEST_CASE("format_hvac_command: test that default HvacState formats as blower off, A/C off, vent",
          "[vehicle_commands]") {
  CommandBuffer buffer{};
  const auto hvac = c3::vehicle::HvacState{};
  CHECK(format(hvac, buffer) == R"({"cmd":"hvac","blower":0,"ac":false,"mode":"vent"})");
}

TEST_CASE("format_hvac_command: test that floor mode formats with blower level", "[vehicle_commands]") {
  CommandBuffer buffer{};
  const auto hvac = c3::vehicle::HvacState{
      .mode = c3::vehicle::HvacMode::Floor,
      .blower = c3::vehicle::HvacBlower::L1,
      .ac_enabled = false,
  };
  CHECK(format(hvac, buffer) == R"({"cmd":"hvac","blower":1,"ac":false,"mode":"floor"})");
}

TEST_CASE("format_hvac_command: test that defrost mode with max blower formats correctly", "[vehicle_commands]") {
  CommandBuffer buffer{};
  const auto hvac = c3::vehicle::HvacState{
      .mode = c3::vehicle::HvacMode::Defrost,
      .blower = c3::vehicle::HvacBlower::L4,
      .ac_enabled = true,
  };
  CHECK(format(hvac, buffer) == R"({"cmd":"hvac","blower":4,"ac":true,"mode":"defrost"})");
}

TEST_CASE("format_hvac_command: test that returned length matches the written text", "[vehicle_commands]") {
  CommandBuffer buffer{};
  const auto hvac = c3::vehicle::HvacState{.blower = c3::vehicle::HvacBlower::L3};
  const auto length = c3::vehicle::format_hvac_command(hvac, buffer);
  const auto expected = std::string_view(R"({"cmd":"hvac","blower":3,"ac":false,"mode":"vent"})");
  CHECK(length == expected.size());
  CHECK(std::string_view(buffer.data(), length) == expected);
}

TEST_CASE("format_hvac_command: test that longest command fits within kMaxCommandLength", "[vehicle_commands]") {
  CommandBuffer buffer{};
  const auto hvac = c3::vehicle::HvacState{
      .mode = c3::vehicle::HvacMode::Defrost,
      .blower = c3::vehicle::HvacBlower::L4,
      .ac_enabled = false,
  };
  const auto length = c3::vehicle::format_hvac_command(hvac, buffer);
  CHECK(length > 0);
  CHECK(length <= c3::vehicle::kMaxCommandLength);
}

TEST_CASE("format_hvac_command: test that exactly-sized output buffer succeeds", "[vehicle_commands]") {
  CommandBuffer full{};
  const auto hvac = c3::vehicle::HvacState{.blower = c3::vehicle::HvacBlower::L2, .ac_enabled = true};
  const auto expected = format(hvac, full);

  CommandBuffer exact{};
  const auto length = c3::vehicle::format_hvac_command(hvac, std::span<char>(exact.data(), expected.size()));
  CHECK(length == expected.size());
  CHECK(std::string_view(exact.data(), length) == expected);
}

TEST_CASE("format_hvac_command: test that too-small output buffer returns 0", "[vehicle_commands]") {
  CommandBuffer full{};
  const auto hvac = c3::vehicle::HvacState{.blower = c3::vehicle::HvacBlower::L2, .ac_enabled = true};
  const auto expected = format(hvac, full);

  CommandBuffer small{};
  CHECK(c3::vehicle::format_hvac_command(hvac, std::span<char>(small.data(), expected.size() - 1)) == 0);
  CHECK(c3::vehicle::format_hvac_command(hvac, std::span<char>(small.data(), 0)) == 0);
}

TEST_CASE("format_hvac_command: test that bytes beyond the returned length are untouched", "[vehicle_commands]") {
  CommandBuffer buffer{};
  buffer.fill('#');
  const auto hvac = c3::vehicle::HvacState{};
  const auto length = c3::vehicle::format_hvac_command(hvac, buffer);
  CHECK(length > 0);
  CHECK(length < buffer.size());
  CHECK(
      std::all_of(buffer.begin() + static_cast<std::ptrdiff_t>(length), buffer.end(), [](char c) { return c == '#'; }));
}

TEST_CASE("format_hvac_command: test that output round-trips through FlatJsonObject", "[vehicle_commands]") {
  CommandBuffer buffer{};
  const auto hvac = c3::vehicle::HvacState{
      .mode = c3::vehicle::HvacMode::Floor,
      .blower = c3::vehicle::HvacBlower::L3,
      .ac_enabled = true,
  };
  auto object = c3::utils::FlatJsonObject(format(hvac, buffer));
  c3::utils::JsonMember member;

  CHECK(object.next(member));
  CHECK(member.key == "cmd");
  CHECK(member.value == "hvac");
  CHECK(member.is_string == true);

  CHECK(object.next(member));
  CHECK(member.key == "blower");
  CHECK(member.value == "3");
  CHECK(member.is_string == false);

  CHECK(object.next(member));
  CHECK(member.key == "ac");
  CHECK(member.value == "true");
  CHECK(member.is_string == false);

  CHECK(object.next(member));
  CHECK(member.key == "mode");
  CHECK(member.value == "floor");
  CHECK(member.is_string == true);

  CHECK_FALSE(object.next(member));
  CHECK(object.complete());
}

// ---------------------------------------------------------------------------
// format_lamp_command
// ---------------------------------------------------------------------------

TEST_CASE("format_lamp_command: test that LampState returns expected JSON line commands", "[vehicle_commands]") {
  CommandBuffer buffer{};
  const auto lamp = c3::vehicle::LampState{
      .lamp_doors_open = true,
      .headlights_on = true,
      .highbeams_on = false,
  };
  CHECK(format(lamp, buffer) == R"({"cmd":"lamp","headlights":true,"doors":true,"high":false})");
}

TEST_CASE("format_lamp_command: test that default LampState formats all relays off", "[vehicle_commands]") {
  CommandBuffer buffer{};
  const auto lamp = c3::vehicle::LampState{};
  CHECK(format(lamp, buffer) == R"({"cmd":"lamp","headlights":false,"doors":false,"high":false})");
}

TEST_CASE("format_lamp_command: test that lamp doors open alone formats correctly", "[vehicle_commands]") {
  CommandBuffer buffer{};
  const auto lamp = c3::vehicle::LampState{.lamp_doors_open = true};
  CHECK(format(lamp, buffer) == R"({"cmd":"lamp","headlights":false,"doors":true,"high":false})");
}

TEST_CASE("format_lamp_command: test that all relays on formats correctly", "[vehicle_commands]") {
  CommandBuffer buffer{};
  const auto lamp = c3::vehicle::LampState{
      .lamp_doors_open = true,
      .headlights_on = true,
      .highbeams_on = true,
  };
  CHECK(format(lamp, buffer) == R"({"cmd":"lamp","headlights":true,"doors":true,"high":true})");
}

TEST_CASE("format_lamp_command: test that returned length matches the written text", "[vehicle_commands]") {
  CommandBuffer buffer{};
  const auto lamp = c3::vehicle::LampState{.lamp_doors_open = true, .headlights_on = true};
  const auto length = c3::vehicle::format_lamp_command(lamp, buffer);
  const auto expected = std::string_view(R"({"cmd":"lamp","headlights":true,"doors":true,"high":false})");
  CHECK(length == expected.size());
  CHECK(std::string_view(buffer.data(), length) == expected);
}

TEST_CASE("format_lamp_command: test that longest command fits within kMaxCommandLength", "[vehicle_commands]") {
  CommandBuffer buffer{};
  const auto lamp = c3::vehicle::LampState{};  // "false" is longer than "true" for every field
  const auto length = c3::vehicle::format_lamp_command(lamp, buffer);
  CHECK(length > 0);
  CHECK(length <= c3::vehicle::kMaxCommandLength);
}

TEST_CASE("format_lamp_command: test that exactly-sized output buffer succeeds", "[vehicle_commands]") {
  CommandBuffer full{};
  const auto lamp = c3::vehicle::LampState{};
  const auto expected = format(lamp, full);

  CommandBuffer exact{};
  const auto length = c3::vehicle::format_lamp_command(lamp, std::span<char>(exact.data(), expected.size()));
  CHECK(length == expected.size());
  CHECK(std::string_view(exact.data(), length) == expected);
}

TEST_CASE("format_lamp_command: test that too-small output buffer returns 0", "[vehicle_commands]") {
  CommandBuffer full{};
  const auto lamp = c3::vehicle::LampState{};
  const auto expected = format(lamp, full);

  CommandBuffer small{};
  CHECK(c3::vehicle::format_lamp_command(lamp, std::span<char>(small.data(), expected.size() - 1)) == 0);
  CHECK(c3::vehicle::format_lamp_command(lamp, std::span<char>(small.data(), 0)) == 0);
}

TEST_CASE("format_lamp_command: test that bytes beyond the returned length are untouched", "[vehicle_commands]") {
  CommandBuffer buffer{};
  buffer.fill('#');
  const auto lamp = c3::vehicle::LampState{};
  const auto length = c3::vehicle::format_lamp_command(lamp, buffer);
  CHECK(length > 0);
  CHECK(length < buffer.size());
  CHECK(
      std::all_of(buffer.begin() + static_cast<std::ptrdiff_t>(length), buffer.end(), [](char c) { return c == '#'; }));
}

TEST_CASE("format_lamp_command: test that output round-trips through FlatJsonObject", "[vehicle_commands]") {
  CommandBuffer buffer{};
  const auto lamp = c3::vehicle::LampState{
      .lamp_doors_open = true,
      .headlights_on = true,
      .highbeams_on = true,
  };
  auto object = c3::utils::FlatJsonObject(format(lamp, buffer));
  c3::utils::JsonMember member;

  CHECK(object.next(member));
  CHECK(member.key == "cmd");
  CHECK(member.value == "lamp");
  CHECK(member.is_string == true);

  CHECK(object.next(member));
  CHECK(member.key == "headlights");
  CHECK(member.value == "true");
  CHECK(member.is_string == false);

  CHECK(object.next(member));
  CHECK(member.key == "doors");
  CHECK(member.value == "true");
  CHECK(member.is_string == false);

  CHECK(object.next(member));
  CHECK(member.key == "high");
  CHECK(member.value == "true");
  CHECK(member.is_string == false);

  CHECK_FALSE(object.next(member));
  CHECK(object.complete());
}

// ---------------------------------------------------------------------------
// parse_acknowledgement
// ---------------------------------------------------------------------------

TEST_CASE("parse_acknowledgement: test that `kind` parses correctly from valid JSON line", "[vehicle_commands]") {
  const auto hvac = c3::vehicle::parse_acknowledgement(R"({"ack":"hvac","ok":true})");
  REQUIRE(hvac.has_value());
  CHECK(hvac->kind == c3::vehicle::CommandKind::Hvac);

  const auto lamp = c3::vehicle::parse_acknowledgement(R"({"ack":"lamp","ok":true})");
  REQUIRE(lamp.has_value());
  CHECK(lamp->kind == c3::vehicle::CommandKind::Lamp);
}

TEST_CASE("parse_acknowledgement: test that `ok` parses correctly from valid JSON line", "[vehicle_commands]") {
  const auto accepted = c3::vehicle::parse_acknowledgement(R"({"ack":"hvac","ok":true})");
  REQUIRE(accepted.has_value());
  CHECK(accepted->ok == true);

  const auto refused = c3::vehicle::parse_acknowledgement(R"({"ack":"hvac","ok":false})");
  REQUIRE(refused.has_value());
  CHECK(refused->ok == false);
}

TEST_CASE("parse_acknowledgement: test that member order does not matter", "[vehicle_commands]") {
  const auto ack = c3::vehicle::parse_acknowledgement(R"({"ok":false,"ack":"lamp"})");
  REQUIRE(ack.has_value());
  CHECK(ack->kind == c3::vehicle::CommandKind::Lamp);
  CHECK(ack->ok == false);
}

TEST_CASE("parse_acknowledgement: test that unknown members are ignored", "[vehicle_commands]") {
  const auto ack = c3::vehicle::parse_acknowledgement(R"({"seq":42,"ack":"hvac","reason":"none","ok":true})");
  REQUIRE(ack.has_value());
  CHECK(ack->kind == c3::vehicle::CommandKind::Hvac);
  CHECK(ack->ok == true);
}

TEST_CASE("parse_acknowledgement: test that whitespace and trailing carriage return are tolerated",
          "[vehicle_commands]") {
  const auto ack = c3::vehicle::parse_acknowledgement(" { \"ack\" : \"lamp\" ,\t\"ok\" : true } \r");
  REQUIRE(ack.has_value());
  CHECK(ack->kind == c3::vehicle::CommandKind::Lamp);
  CHECK(ack->ok == true);
}

TEST_CASE("parse_acknowledgement: test that `nullopt` return when invalid JSON line", "[vehicle_commands]") {
  CHECK_FALSE(c3::vehicle::parse_acknowledgement("").has_value());
  CHECK_FALSE(c3::vehicle::parse_acknowledgement("not json").has_value());
  CHECK_FALSE(c3::vehicle::parse_acknowledgement(R"("ack":"hvac","ok":true})").has_value());
  CHECK_FALSE(c3::vehicle::parse_acknowledgement(R"({"ack":"hvac","ok":true)").has_value());
  CHECK_FALSE(c3::vehicle::parse_acknowledgement(R"({"ack":"hvac","ok":true}})").has_value());
  CHECK_FALSE(c3::vehicle::parse_acknowledgement(R"({"ack":"hvac","ok":true,})").has_value());
  CHECK_FALSE(c3::vehicle::parse_acknowledgement(R"({"ack":"hvac" "ok":true})").has_value());
}

TEST_CASE("parse_acknowledgement: test that `nullopt` return when JSON is nested", "[vehicle_commands]") {
  CHECK_FALSE(c3::vehicle::parse_acknowledgement(R"({"ack":{"kind":"hvac"},"ok":true})").has_value());
  CHECK_FALSE(c3::vehicle::parse_acknowledgement(R"({"ack":"hvac","ok":true,"relays":[1,2]})").has_value());
}

TEST_CASE("parse_acknowledgement: test that `nullopt` return when `kind` is invalid data type", "[vehicle_commands]") {
  CHECK_FALSE(c3::vehicle::parse_acknowledgement(R"({"ack":1,"ok":true})").has_value());
  CHECK_FALSE(c3::vehicle::parse_acknowledgement(R"({"ack":true,"ok":true})").has_value());
  CHECK_FALSE(c3::vehicle::parse_acknowledgement(R"({"ack":hvac,"ok":true})").has_value());
}

TEST_CASE("parse_acknowledgement: test that `nullopt` return when `kind` is an unknown command", "[vehicle_commands]") {
  CHECK_FALSE(c3::vehicle::parse_acknowledgement(R"({"ack":"wiper","ok":true})").has_value());
  CHECK_FALSE(c3::vehicle::parse_acknowledgement(R"({"ack":"HVAC","ok":true})").has_value());
  CHECK_FALSE(c3::vehicle::parse_acknowledgement(R"({"ack":"","ok":true})").has_value());
}

TEST_CASE("parse_acknowledgement: test that `nullopt` return when `ok` is invalid data type", "[vehicle_commands]") {
  CHECK_FALSE(c3::vehicle::parse_acknowledgement(R"({"ack":"hvac","ok":"true"})").has_value());
  CHECK_FALSE(c3::vehicle::parse_acknowledgement(R"({"ack":"hvac","ok":1})").has_value());
  CHECK_FALSE(c3::vehicle::parse_acknowledgement(R"({"ack":"hvac","ok":yes})").has_value());
  CHECK_FALSE(c3::vehicle::parse_acknowledgement(R"({"ack":"hvac","ok":True})").has_value());
}

TEST_CASE("parse_acknowledgement: test that `nullopt` return when a required member is missing", "[vehicle_commands]") {
  CHECK_FALSE(c3::vehicle::parse_acknowledgement(R"({"ack":"hvac"})").has_value());
  CHECK_FALSE(c3::vehicle::parse_acknowledgement(R"({"ok":true})").has_value());
  CHECK_FALSE(c3::vehicle::parse_acknowledgement("{}").has_value());
}

TEST_CASE("parse_acknowledgement: test that `nullopt` return when a required member is duplicated",
          "[vehicle_commands]") {
  CHECK_FALSE(c3::vehicle::parse_acknowledgement(R"({"ack":"hvac","ack":"lamp","ok":true})").has_value());
  CHECK_FALSE(c3::vehicle::parse_acknowledgement(R"({"ack":"hvac","ok":true,"ok":false})").has_value());
}

// ---------------------------------------------------------------------------
// to_string(CommandStatus)
// ---------------------------------------------------------------------------

TEST_CASE("to_string: test that CommandStatus::Idle returns blank string", "[vehicle_commands]") {
  const auto status = c3::vehicle::CommandStatus::Idle;
  CHECK(c3::vehicle::to_string(status) == "");
}

TEST_CASE("to_string: test that CommandStatus::Pending returns 'SENDING'", "[vehicle_commands]") {
  const auto status = c3::vehicle::CommandStatus::Pending;
  CHECK(c3::vehicle::to_string(status) == "SENDING");
}

TEST_CASE("to_string: test that CommandStatus::Acknowledged returns 'WAITING FOR STATE'", "[vehicle_commands]") {
  const auto status = c3::vehicle::CommandStatus::Acknowledged;
  CHECK(c3::vehicle::to_string(status) == "WAITING FOR STATE");
}

TEST_CASE("to_string: test that CommandStatus::Confirmed returns blank string", "[vehicle_commands]") {
  const auto status = c3::vehicle::CommandStatus::Confirmed;
  CHECK(c3::vehicle::to_string(status) == "");
}

TEST_CASE("to_string: test that CommandStatus::Rejected returns 'REFUSED'", "[vehicle_commands]") {
  const auto status = c3::vehicle::CommandStatus::Rejected;
  CHECK(c3::vehicle::to_string(status) == "REFUSED");
}

TEST_CASE("to_string: test that CommandStatus::TimedOut returns 'NO CONFIRMATION'", "[vehicle_commands]") {
  const auto status = c3::vehicle::CommandStatus::TimedOut;
  CHECK(c3::vehicle::to_string(status) == "NO CONFIRMATION");
}

TEST_CASE("to_string: test that CommandStatus::Unavailable returns 'UNAVAILABLE'", "[vehicle_commands]") {
  const auto status = c3::vehicle::CommandStatus::Unavailable;
  CHECK(c3::vehicle::to_string(status) == "UNAVAILABLE");
}

}  // namespace
