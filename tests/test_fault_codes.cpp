#include "telemetry/fault_codes.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

using c3::telemetry::Channel;
using c3::telemetry::Fault;
using c3::telemetry::FaultKind;
using c3::telemetry::format_fault_code;
using c3::telemetry::has_channel;
using c3::telemetry::kChannelCount;
using c3::telemetry::kFaultKindCount;
using c3::telemetry::kMaxFaultCodeLength;
using c3::telemetry::parse_channel;
using c3::telemetry::parse_fault_code;

namespace {

using CodeBuffer = std::array<char, 64>;

/// Formats into a generously sized buffer and returns the written text as a view.
[[nodiscard]] std::string_view format(Fault fault, CodeBuffer& buffer) noexcept {
  const auto length = format_fault_code(fault, buffer);
  return {buffer.data(), length};
}

[[nodiscard]] constexpr FaultKind kind_at(std::size_t i) noexcept {
  return static_cast<FaultKind>(i);
}
[[nodiscard]] constexpr Channel channel_at(std::size_t i) noexcept {
  return static_cast<Channel>(i);
}

constexpr std::array<FaultKind, 5> kSimpleKinds{FaultKind::OilLow, FaultKind::CltHigh, FaultKind::FuelLow,
                                                FaultKind::VoltLow, FaultKind::VoltHigh};
constexpr std::array<FaultKind, 2> kChannelKinds{FaultKind::SenderOpen, FaultKind::SenderShort};

// ---------------------------------------------------------------------------
// has_channel
// ---------------------------------------------------------------------------

TEST_CASE("has_channel: true only for the sender fault kinds", "[fault_codes]") {
  STATIC_CHECK(has_channel(FaultKind::SenderOpen));
  STATIC_CHECK(has_channel(FaultKind::SenderShort));
  STATIC_CHECK_FALSE(has_channel(FaultKind::OilLow));
  STATIC_CHECK_FALSE(has_channel(FaultKind::CltHigh));
  STATIC_CHECK_FALSE(has_channel(FaultKind::FuelLow));
  STATIC_CHECK_FALSE(has_channel(FaultKind::VoltLow));
  STATIC_CHECK_FALSE(has_channel(FaultKind::VoltHigh));
}

TEST_CASE("has_channel: every kind is classified one way or the other", "[fault_codes]") {
  std::size_t with_channel = 0;
  for (std::size_t i = 0; i < kFaultKindCount; ++i) {
    if (has_channel(kind_at(i))) {
      ++with_channel;
    }
  }
  CHECK(with_channel == kChannelKinds.size());
  CHECK(kFaultKindCount - with_channel == kSimpleKinds.size());
}

// ---------------------------------------------------------------------------
// parse_channel
// ---------------------------------------------------------------------------

TEST_CASE("parse_channel: recognises every channel name", "[fault_codes]") {
  REQUIRE(parse_channel("FUEL").has_value());
  CHECK(*parse_channel("FUEL") == Channel::Fuel);
  REQUIRE(parse_channel("OIL").has_value());
  CHECK(*parse_channel("OIL") == Channel::Oil);
  REQUIRE(parse_channel("COOLANT").has_value());
  CHECK(*parse_channel("COOLANT") == Channel::Coolant);
  REQUIRE(parse_channel("VOLT").has_value());
  CHECK(*parse_channel("VOLT") == Channel::Volt);
}

TEST_CASE("parse_channel: round-trips to_string for every channel", "[fault_codes]") {
  for (std::size_t i = 0; i < kChannelCount; ++i) {
    const auto channel = channel_at(i);
    const auto parsed = parse_channel(to_string(channel));
    REQUIRE(parsed.has_value());
    CHECK(*parsed == channel);
  }
}

TEST_CASE("parse_channel: rejects empty, case-mismatched and padded names", "[fault_codes]") {
  CHECK_FALSE(parse_channel("").has_value());
  CHECK_FALSE(parse_channel("fuel").has_value());
  CHECK_FALSE(parse_channel("Fuel").has_value());
  CHECK_FALSE(parse_channel(" FUEL").has_value());
  CHECK_FALSE(parse_channel("FUEL ").has_value());
  CHECK_FALSE(parse_channel("FUELX").has_value());
  CHECK_FALSE(parse_channel("FUE").has_value());
}

TEST_CASE("parse_channel: never yields the Count sentinel", "[fault_codes]") {
  CHECK_FALSE(parse_channel("COUNT").has_value());
  CHECK_FALSE(parse_channel("?").has_value());  // to_string(Channel::Count) falls through to "?"
}

// ---------------------------------------------------------------------------
// parse_fault_code: accepted codes
// ---------------------------------------------------------------------------

TEST_CASE("parse_fault_code: parses every simple code", "[fault_codes]") {
  struct Case {
    std::string_view code;
    FaultKind kind;
  };
  constexpr std::array<Case, 5> cases{{
      {"OIL_LOW", FaultKind::OilLow},
      {"CLT_HIGH", FaultKind::CltHigh},
      {"FUEL_LOW", FaultKind::FuelLow},
      {"VOLT_LOW", FaultKind::VoltLow},
      {"VOLT_HIGH", FaultKind::VoltHigh},
  }};
  for (const auto& c : cases) {
    const auto fault = parse_fault_code(c.code);
    REQUIRE(fault.has_value());
    CHECK(fault->kind == c.kind);
    CHECK(fault->channel == Channel{});
  }
}

TEST_CASE("parse_fault_code: parses SENDER_OPEN with every channel", "[fault_codes]") {
  struct Case {
    std::string_view code;
    Channel channel;
  };
  constexpr std::array<Case, 4> cases{{
      {"SENDER_OPEN_FUEL", Channel::Fuel},
      {"SENDER_OPEN_OIL", Channel::Oil},
      {"SENDER_OPEN_COOLANT", Channel::Coolant},
      {"SENDER_OPEN_VOLT", Channel::Volt},
  }};
  for (const auto& c : cases) {
    const auto fault = parse_fault_code(c.code);
    REQUIRE(fault.has_value());
    CHECK(fault->kind == FaultKind::SenderOpen);
    CHECK(fault->channel == c.channel);
  }
}

TEST_CASE("parse_fault_code: parses SENDER_SHORT with every channel", "[fault_codes]") {
  struct Case {
    std::string_view code;
    Channel channel;
  };
  constexpr std::array<Case, 4> cases{{
      {"SENDER_SHORT_FUEL", Channel::Fuel},
      {"SENDER_SHORT_OIL", Channel::Oil},
      {"SENDER_SHORT_COOLANT", Channel::Coolant},
      {"SENDER_SHORT_VOLT", Channel::Volt},
  }};
  for (const auto& c : cases) {
    const auto fault = parse_fault_code(c.code);
    REQUIRE(fault.has_value());
    CHECK(fault->kind == FaultKind::SenderShort);
    CHECK(fault->channel == c.channel);
  }
}

// ---------------------------------------------------------------------------
// parse_fault_code: rejected codes
// ---------------------------------------------------------------------------

TEST_CASE("parse_fault_code: rejects empty and unknown codes", "[fault_codes]") {
  CHECK_FALSE(parse_fault_code("").has_value());
  CHECK_FALSE(parse_fault_code("UNKNOWN").has_value());
  CHECK_FALSE(parse_fault_code("OIL").has_value());
  CHECK_FALSE(parse_fault_code("LOW").has_value());
  CHECK_FALSE(parse_fault_code("_").has_value());
}

TEST_CASE("parse_fault_code: rejects case mismatches", "[fault_codes]") {
  CHECK_FALSE(parse_fault_code("oil_low").has_value());
  CHECK_FALSE(parse_fault_code("Oil_Low").has_value());
  CHECK_FALSE(parse_fault_code("sender_open_fuel").has_value());
  CHECK_FALSE(parse_fault_code("SENDER_OPEN_fuel").has_value());
}

TEST_CASE("parse_fault_code: rejects simple codes with extra characters", "[fault_codes]") {
  CHECK_FALSE(parse_fault_code("OIL_LOWX").has_value());
  CHECK_FALSE(parse_fault_code("OIL_LO").has_value());
  CHECK_FALSE(parse_fault_code(" OIL_LOW").has_value());
  CHECK_FALSE(parse_fault_code("OIL_LOW ").has_value());
  CHECK_FALSE(parse_fault_code("OIL_LOW\n").has_value());
}

TEST_CASE("parse_fault_code: rejects a channel suffix on a simple kind", "[fault_codes]") {
  CHECK_FALSE(parse_fault_code("OIL_LOW_OIL").has_value());
  CHECK_FALSE(parse_fault_code("VOLT_HIGH_VOLT").has_value());
  CHECK_FALSE(parse_fault_code("FUEL_LOW_FUEL").has_value());
}

TEST_CASE("parse_fault_code: rejects a sender kind without a channel suffix", "[fault_codes]") {
  CHECK_FALSE(parse_fault_code("SENDER_OPEN").has_value());
  CHECK_FALSE(parse_fault_code("SENDER_SHORT").has_value());
  CHECK_FALSE(parse_fault_code("SENDER_OPEN_").has_value());
  CHECK_FALSE(parse_fault_code("SENDER_SHORT_").has_value());
}

TEST_CASE("parse_fault_code: rejects a sender kind with a malformed suffix", "[fault_codes]") {
  CHECK_FALSE(parse_fault_code("SENDER_OPENFUEL").has_value());    // missing separator
  CHECK_FALSE(parse_fault_code("SENDER_OPEN-FUEL").has_value());   // wrong separator
  CHECK_FALSE(parse_fault_code("SENDER_OPEN__FUEL").has_value());  // doubled separator
  CHECK_FALSE(parse_fault_code("SENDER_OPEN_FUEL_").has_value());  // trailing separator
  CHECK_FALSE(parse_fault_code("SENDER_OPEN_FUELX").has_value());
  CHECK_FALSE(parse_fault_code("SENDER_OPEN_X").has_value());
  CHECK_FALSE(parse_fault_code("SENDER_OPEN_FUEL_OIL").has_value());
}

TEST_CASE("parse_fault_code: rejects the Count sentinels and their fallbacks", "[fault_codes]") {
  CHECK_FALSE(parse_fault_code("COUNT").has_value());
  CHECK_FALSE(parse_fault_code("?").has_value());  // to_string(FaultKind::Count) falls through to "?"
  CHECK_FALSE(parse_fault_code("SENDER_OPEN_COUNT").has_value());
  CHECK_FALSE(parse_fault_code("SENDER_OPEN_?").has_value());
}

TEST_CASE("parse_fault_code: a bare channel name is not a fault", "[fault_codes]") {
  CHECK_FALSE(parse_fault_code("FUEL").has_value());
  CHECK_FALSE(parse_fault_code("VOLT").has_value());
  CHECK_FALSE(parse_fault_code("COOLANT").has_value());
}

// ---------------------------------------------------------------------------
// format_fault_code
// ---------------------------------------------------------------------------

TEST_CASE("format_fault_code: writes every simple code", "[fault_codes]") {
  CodeBuffer buffer{};
  CHECK(format(Fault{.kind = FaultKind::OilLow}, buffer) == "OIL_LOW");
  CHECK(format(Fault{.kind = FaultKind::CltHigh}, buffer) == "CLT_HIGH");
  CHECK(format(Fault{.kind = FaultKind::FuelLow}, buffer) == "FUEL_LOW");
  CHECK(format(Fault{.kind = FaultKind::VoltLow}, buffer) == "VOLT_LOW");
  CHECK(format(Fault{.kind = FaultKind::VoltHigh}, buffer) == "VOLT_HIGH");
}

TEST_CASE("format_fault_code: writes SENDER_OPEN with the channel suffix", "[fault_codes]") {
  CodeBuffer buffer{};
  CHECK(format(Fault{.kind = FaultKind::SenderOpen, .channel = Channel::Fuel}, buffer) == "SENDER_OPEN_FUEL");
  CHECK(format(Fault{.kind = FaultKind::SenderOpen, .channel = Channel::Oil}, buffer) == "SENDER_OPEN_OIL");
  CHECK(format(Fault{.kind = FaultKind::SenderOpen, .channel = Channel::Coolant}, buffer) == "SENDER_OPEN_COOLANT");
  CHECK(format(Fault{.kind = FaultKind::SenderOpen, .channel = Channel::Volt}, buffer) == "SENDER_OPEN_VOLT");
}

TEST_CASE("format_fault_code: writes SENDER_SHORT with the channel suffix", "[fault_codes]") {
  CodeBuffer buffer{};
  CHECK(format(Fault{.kind = FaultKind::SenderShort, .channel = Channel::Fuel}, buffer) == "SENDER_SHORT_FUEL");
  CHECK(format(Fault{.kind = FaultKind::SenderShort, .channel = Channel::Oil}, buffer) == "SENDER_SHORT_OIL");
  CHECK(format(Fault{.kind = FaultKind::SenderShort, .channel = Channel::Coolant}, buffer) == "SENDER_SHORT_COOLANT");
  CHECK(format(Fault{.kind = FaultKind::SenderShort, .channel = Channel::Volt}, buffer) == "SENDER_SHORT_VOLT");
}

TEST_CASE("format_fault_code: simple kinds ignore the channel field", "[fault_codes]") {
  CodeBuffer buffer{};
  for (const auto kind : kSimpleKinds) {
    const auto plain = format(Fault{.kind = kind, .channel = Channel::Fuel}, buffer);
    CodeBuffer other{};
    const auto with_coolant = format(Fault{.kind = kind, .channel = Channel::Coolant}, other);
    CHECK(plain == with_coolant);
    CHECK(plain == to_string(kind));
  }
}

TEST_CASE("format_fault_code: returned length matches the text written", "[fault_codes]") {
  CodeBuffer buffer{};
  const auto length = format_fault_code(Fault{.kind = FaultKind::SenderOpen, .channel = Channel::Oil}, buffer);
  CHECK(length == std::string_view("SENDER_OPEN_OIL").size());
  CHECK(std::string_view(buffer.data(), length) == "SENDER_OPEN_OIL");
}

TEST_CASE("format_fault_code: exactly-sized output buffer succeeds", "[fault_codes]") {
  CodeBuffer full{};
  const Fault fault{.kind = FaultKind::SenderShort, .channel = Channel::Volt};
  const auto expected = format(fault, full);

  CodeBuffer exact{};
  const auto length = format_fault_code(fault, std::span<char>(exact.data(), expected.size()));
  CHECK(length == expected.size());
  CHECK(std::string_view(exact.data(), length) == expected);
}

TEST_CASE("format_fault_code: too-small output buffer returns 0 and writes nothing", "[fault_codes]") {
  CodeBuffer full{};
  const Fault fault{.kind = FaultKind::SenderShort, .channel = Channel::Volt};
  const auto expected = format(fault, full);

  CodeBuffer small{};
  small.fill('#');
  CHECK(format_fault_code(fault, std::span<char>(small.data(), expected.size() - 1)) == 0);
  CHECK(format_fault_code(fault, std::span<char>(small.data(), 0)) == 0);
  CHECK(std::all_of(small.begin(), small.end(), [](char c) { return c == '#'; }));
}

TEST_CASE("format_fault_code: too-small buffer for a simple code also returns 0", "[fault_codes]") {
  CodeBuffer small{};
  small.fill('#');
  CHECK(format_fault_code(Fault{.kind = FaultKind::OilLow}, std::span<char>(small.data(), 6)) == 0);
  CHECK(std::all_of(small.begin(), small.end(), [](char c) { return c == '#'; }));
}

TEST_CASE("format_fault_code: bytes beyond the returned length are untouched", "[fault_codes]") {
  CodeBuffer buffer{};
  buffer.fill('#');
  const auto length = format_fault_code(Fault{.kind = FaultKind::CltHigh}, buffer);
  REQUIRE(length > 0);
  CHECK(
      std::all_of(buffer.begin() + static_cast<std::ptrdiff_t>(length), buffer.end(), [](char c) { return c == '#'; }));
}

// ---------------------------------------------------------------------------
// kMaxFaultCodeLength
// ---------------------------------------------------------------------------

TEST_CASE("kMaxFaultCodeLength: every fault code fits in a buffer of that size", "[fault_codes]") {
  std::array<char, kMaxFaultCodeLength> buffer{};
  for (const auto kind : kSimpleKinds) {
    CHECK(format_fault_code(Fault{.kind = kind}, buffer) > 0);
  }
  for (const auto kind : kChannelKinds) {
    for (std::size_t c = 0; c < kChannelCount; ++c) {
      INFO("kind=" << to_string(kind) << " channel=" << to_string(channel_at(c)));
      CHECK(format_fault_code(Fault{.kind = kind, .channel = channel_at(c)}, buffer) > 0);
    }
  }
}

TEST_CASE("kMaxFaultCodeLength: equals the length of the longest fault code", "[fault_codes]") {
  CodeBuffer buffer{};
  std::size_t longest = 0;
  for (const auto kind : kSimpleKinds) {
    longest = std::max(longest, format(Fault{.kind = kind}, buffer).size());
  }
  for (const auto kind : kChannelKinds) {
    for (std::size_t c = 0; c < kChannelCount; ++c) {
      longest = std::max(longest, format(Fault{.kind = kind, .channel = channel_at(c)}, buffer).size());
    }
  }
  CHECK(longest == kMaxFaultCodeLength);
}

// ---------------------------------------------------------------------------
// Round trips
// ---------------------------------------------------------------------------

TEST_CASE("round trip: format then parse recovers every simple fault", "[fault_codes]") {
  CodeBuffer buffer{};
  for (const auto kind : kSimpleKinds) {
    const auto parsed = parse_fault_code(format(Fault{.kind = kind}, buffer));
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == kind);
  }
}

TEST_CASE("round trip: format then parse recovers every sender fault and channel", "[fault_codes]") {
  CodeBuffer buffer{};
  for (const auto kind : kChannelKinds) {
    for (std::size_t c = 0; c < kChannelCount; ++c) {
      const Fault fault{.kind = kind, .channel = channel_at(c)};
      const auto parsed = parse_fault_code(format(fault, buffer));
      REQUIRE(parsed.has_value());
      CHECK(parsed->kind == fault.kind);
      CHECK(parsed->channel == fault.channel);
    }
  }
}

TEST_CASE("round trip: parse then format reproduces the wire text exactly", "[fault_codes]") {
  constexpr std::array<std::string_view, 13> codes{
      "OIL_LOW",           "CLT_HIGH",          "FUEL_LOW",         "VOLT_LOW",
      "VOLT_HIGH",         "SENDER_OPEN_FUEL",  "SENDER_OPEN_OIL",  "SENDER_OPEN_COOLANT",
      "SENDER_OPEN_VOLT",  "SENDER_SHORT_FUEL", "SENDER_SHORT_OIL", "SENDER_SHORT_COOLANT",
      "SENDER_SHORT_VOLT",
  };
  CodeBuffer buffer{};
  for (const auto code : codes) {
    const auto parsed = parse_fault_code(code);
    REQUIRE(parsed.has_value());
    CHECK(format(*parsed, buffer) == code);
  }
}

TEST_CASE("round trip: every distinct fault formats to a distinct code", "[fault_codes]") {
  // 5 simple kinds plus 2 sender kinds x 4 channels.
  std::array<std::array<char, 64>, 13> storage{};
  std::array<std::string_view, 13> codes{};
  std::size_t n = 0;
  for (const auto kind : kSimpleKinds) {
    codes[n] = format(Fault{.kind = kind}, storage[n]);
    ++n;
  }
  for (const auto kind : kChannelKinds) {
    for (std::size_t c = 0; c < kChannelCount; ++c) {
      codes[n] = format(Fault{.kind = kind, .channel = channel_at(c)}, storage[n]);
      ++n;
    }
  }
  REQUIRE(n == codes.size());
  for (std::size_t i = 0; i < codes.size(); ++i) {
    for (std::size_t j = i + 1; j < codes.size(); ++j) {
      CHECK(codes[i] != codes[j]);
    }
  }
}

}  // namespace
