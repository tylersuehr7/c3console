#include "telemetry/crc16.hpp"
#include "telemetry/frame.hpp"
#include "telemetry/protocol.hpp"
#include "vehicle/vehicle_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using c3::telemetry::Channel;
using c3::telemetry::Crc16Ccitt;
using c3::telemetry::FaultKind;
using c3::telemetry::Frame;
using c3::telemetry::kMaxLineLength;
using c3::telemetry::LineReader;
using c3::telemetry::parse_channel_key;
using c3::telemetry::parse_frame;
using c3::telemetry::ParseError;
using c3::telemetry::ParseResult;
using c3::vehicle::HvacBlower;
using c3::vehicle::HvacMode;
using Catch::Matchers::WithinRel;

CATCH_REGISTER_ENUM(ParseError, ParseError::None, ParseError::Empty, ParseError::TooLong, ParseError::MissingCrc,
                    ParseError::BadCrcField, ParseError::CrcMismatch, ParseError::Malformed, ParseError::MissingField,
                    ParseError::OutOfRange, ParseError::UnknownFault, ParseError::TooManyFaults)

namespace {

// ---------------------------------------------------------------------------
// Line builders
//
// Every valid line in this file is sealed the way the MCU serialises it: the object without
// its closing brace, then `,"crc":"xxxx"}` where the crc covers the original object including
// the brace. Tests allocate freely; the code under test must not.
// ---------------------------------------------------------------------------

/// Two frames whose CRCs were computed by an independent Python implementation of
/// CRC-16/CCITT-FALSE, so the parser is not checked only against the C++ CRC it links.
constexpr std::string_view kOracleSpecLine =
    R"json({"t":184523,"fuel":0.62,"oil":41.2,"clt":196.4,"volt":14.1,"faults":[],"crc":"f8ca"})json";
constexpr std::string_view kOracleFaultLine =
    R"json({"t":1,"fuel":0.5,"oil":40,"clt":180,"volt":14,"faults":["OIL_LOW"],"crc":"21e1"})json";

[[nodiscard]] std::string hex4(std::uint16_t value) {
  std::array<char, 8> buf{};
  std::snprintf(buf.data(), buf.size(), "%04x", value);  // NOLINT(cppcoreguidelines-pro-type-vararg)
  return std::string(buf.data());
}

/// Appends a correct crc member to a complete JSON object.
[[nodiscard]] std::string seal(std::string_view object) {
  REQUIRE_FALSE(object.empty());
  REQUIRE(object.back() == '}');
  std::string line(object.substr(0, object.size() - 1));
  line += R"(,"crc":")";
  line += hex4(Crc16Ccitt::compute(object));
  line += "\"}";
  return line;
}

/// Appends a crc member carrying an arbitrary (usually wrong) value.
[[nodiscard]] std::string seal_with(std::string_view object, std::string_view crc_text) {
  REQUIRE(object.back() == '}');
  std::string line(object.substr(0, object.size() - 1));
  line += R"(,"crc":")";
  line += crc_text;
  line += "\"}";
  return line;
}

struct Scalars {
  std::string_view t = "184523";
  std::string_view fuel = "0.62";
  std::string_view oil = "41.2";
  std::string_view clt = "196.4";
  std::string_view volt = "14.1";
  std::string_view faults = "[]";
};

/// `{"t":…,"fuel":…,"oil":…,"clt":…,"volt":…,"faults":…[,extra]}` unsealed.
[[nodiscard]] std::string object_of(const Scalars& s, std::string_view extra = {}) {
  std::string o = "{\"t\":";
  o += s.t;
  o += ",\"fuel\":";
  o += s.fuel;
  o += ",\"oil\":";
  o += s.oil;
  o += ",\"clt\":";
  o += s.clt;
  o += ",\"volt\":";
  o += s.volt;
  o += ",\"faults\":";
  o += s.faults;
  if (!extra.empty()) {
    o += ",";
    o += extra;
  }
  o += "}";
  return o;
}

[[nodiscard]] std::string line_of(const Scalars& s, std::string_view extra = {}) {
  return seal(object_of(s, extra));
}

/// A sealed frame carrying only the required fields plus `extra`.
[[nodiscard]] std::string frame_with(std::string_view extra) {
  return line_of(Scalars{}, extra);
}

/// A sealed frame with one required field omitted.
[[nodiscard]] std::string frame_without(std::string_view omitted) {
  constexpr std::array<std::pair<std::string_view, std::string_view>, 6> kFields{{
      {"t", "184523"},
      {"fuel", "0.62"},
      {"oil", "41.2"},
      {"clt", "196.4"},
      {"volt", "14.1"},
      {"faults", "[]"},
  }};
  std::string o = "{";
  bool first = true;
  for (const auto& [key, value] : kFields) {
    if (key == omitted) {
      continue;
    }
    if (!first) {
      o += ",";
    }
    first = false;
    o += "\"";
    o += key;
    o += "\":";
    o += value;
  }
  o += "}";
  return seal(o);
}

/// Asserts the ParseResult invariant: a frame is present exactly when the error is None.
void expect_error(std::string_view line, ParseError expected) {
  INFO("line: " << line);
  const ParseResult r = parse_frame(line);
  CHECK(r.error == expected);
  CHECK(r.frame.has_value() == (expected == ParseError::None));
}

[[nodiscard]] Frame expect_frame(std::string_view line) {
  INFO("line: " << line);
  const ParseResult r = parse_frame(line);
  REQUIRE(r.error == ParseError::None);
  REQUIRE(r.frame.has_value());
  return *r.frame;
}

/// Feeds a string through a LineReader, collecting delivered lines as owned strings.
void feed(LineReader& reader, std::string_view bytes, std::vector<std::string>& lines) {
  reader.feed(std::span<const char>(bytes.data(), bytes.size()),
              [&lines](std::string_view line) { lines.emplace_back(line); });
}

// ---------------------------------------------------------------------------
// to_string(ParseError)
// ---------------------------------------------------------------------------

TEST_CASE("to_string: every ParseError has a distinct human-readable name", "[protocol]") {
  using c3::telemetry::to_string;
  CHECK(to_string(ParseError::None) == "ok");
  CHECK(to_string(ParseError::Empty) == "empty");
  CHECK(to_string(ParseError::TooLong) == "too long");
  CHECK(to_string(ParseError::MissingCrc) == "missing crc");
  CHECK(to_string(ParseError::BadCrcField) == "bad crc field");
  CHECK(to_string(ParseError::CrcMismatch) == "crc mismatch");
  CHECK(to_string(ParseError::Malformed) == "malformed");
  CHECK(to_string(ParseError::MissingField) == "missing field");
  CHECK(to_string(ParseError::OutOfRange) == "out of range");
  CHECK(to_string(ParseError::UnknownFault) == "unknown fault");
  CHECK(to_string(ParseError::TooManyFaults) == "too many faults");
}

// ---------------------------------------------------------------------------
// parse_channel_key
// ---------------------------------------------------------------------------

TEST_CASE("parse_channel_key: maps the raw-object keys to channels", "[protocol]") {
  REQUIRE(parse_channel_key("fuel").has_value());
  CHECK(*parse_channel_key("fuel") == Channel::Fuel);
  REQUIRE(parse_channel_key("oil").has_value());
  CHECK(*parse_channel_key("oil") == Channel::Oil);
  REQUIRE(parse_channel_key("clt").has_value());
  CHECK(*parse_channel_key("clt") == Channel::Coolant);
  REQUIRE(parse_channel_key("volt").has_value());
  CHECK(*parse_channel_key("volt") == Channel::Volt);
}

TEST_CASE("parse_channel_key: rejects other spellings", "[protocol]") {
  CHECK_FALSE(parse_channel_key("").has_value());
  CHECK_FALSE(parse_channel_key("FUEL").has_value());
  CHECK_FALSE(parse_channel_key("coolant").has_value());
  CHECK_FALSE(parse_channel_key("CLT").has_value());
  CHECK_FALSE(parse_channel_key("fuel ").has_value());
  CHECK_FALSE(parse_channel_key("t").has_value());
}

// ---------------------------------------------------------------------------
// Golden frames
// ---------------------------------------------------------------------------

TEST_CASE("parse_frame: the spec example with an independently computed crc parses", "[protocol]") {
  const Frame f = expect_frame(kOracleSpecLine);
  CHECK(f.t_ms == 184523U);
  CHECK_THAT(f.fuel_fraction, WithinRel(0.62F, 1e-6F));
  CHECK_THAT(f.oil_psi, WithinRel(41.2F, 1e-6F));
  CHECK_THAT(f.coolant_f, WithinRel(196.4F, 1e-6F));
  CHECK_THAT(f.voltage_v, WithinRel(14.1F, 1e-6F));
  CHECK(f.faults.empty());
}

TEST_CASE("parse_frame: an independently sealed frame with a fault parses", "[protocol]") {
  const Frame f = expect_frame(kOracleFaultLine);
  CHECK(f.t_ms == 1U);
  CHECK(f.fuel_fraction == 0.5F);
  CHECK(f.oil_psi == 40.0F);
  CHECK(f.coolant_f == 180.0F);
  CHECK(f.voltage_v == 14.0F);
  REQUIRE(f.faults.size() == 1);
  CHECK(f.faults[0].kind == FaultKind::OilLow);
}

TEST_CASE("seal: the test-side sealer agrees with the independent oracle", "[protocol]") {
  CHECK(line_of(Scalars{}) == kOracleSpecLine);
  CHECK(seal(R"json({"t":1,"fuel":0.5,"oil":40,"clt":180,"volt":14,"faults":["OIL_LOW"]})json") == kOracleFaultLine);
}

TEST_CASE("parse_frame: optional members default when absent", "[protocol]") {
  const Frame f = expect_frame(line_of(Scalars{}));
  CHECK(f.faults.empty());
  for (const auto raw : f.raw) {
    CHECK(raw == 0);
  }
  CHECK_FALSE(f.relays.has_value());
  CHECK_FALSE(f.reverse);
}

TEST_CASE("parse_frame: a complete frame populates every member", "[protocol]") {
  const std::string line = frame_with(
      R"json("raw":{"fuel":12843,"oil":8211,"clt":19004,"volt":21877},)json"
      R"json("relays":{"blower":2,"ac":true,"mode":"vent","headlights":false,"doors":true,"high":false},)json"
      R"json("rev":true)json");
  const Frame f = expect_frame(line);

  CHECK(f.t_ms == 184523U);
  CHECK_THAT(f.fuel_fraction, WithinRel(0.62F, 1e-6F));
  CHECK_THAT(f.oil_psi, WithinRel(41.2F, 1e-6F));
  CHECK_THAT(f.coolant_f, WithinRel(196.4F, 1e-6F));
  CHECK_THAT(f.voltage_v, WithinRel(14.1F, 1e-6F));
  CHECK(f.faults.empty());

  CHECK(f.raw[static_cast<std::size_t>(Channel::Fuel)] == 12843);
  CHECK(f.raw[static_cast<std::size_t>(Channel::Oil)] == 8211);
  CHECK(f.raw[static_cast<std::size_t>(Channel::Coolant)] == 19004);
  CHECK(f.raw[static_cast<std::size_t>(Channel::Volt)] == 21877);

  REQUIRE(f.relays.has_value());
  CHECK(f.relays->blower == HvacBlower::L2);
  CHECK(f.relays->ac_enabled == true);
  CHECK(f.relays->mode == HvacMode::Vent);
  CHECK(f.relays->headlights_on == false);
  REQUIRE(f.relays->lamp_doors_open.has_value());
  CHECK(*f.relays->lamp_doors_open == true);
  REQUIRE(f.relays->highbeams_on.has_value());
  CHECK(*f.relays->highbeams_on == false);

  CHECK(f.reverse == true);
}

TEST_CASE("parse_frame: member order does not matter", "[protocol]") {
  const std::string line = seal(
      R"json({"rev":true,"faults":["CLT_HIGH"],"volt":14.1,"raw":{"oil":8211},"clt":196.4,"oil":41.2,"fuel":0.62,"t":184523})json");
  const Frame f = expect_frame(line);
  CHECK(f.t_ms == 184523U);
  CHECK_THAT(f.fuel_fraction, WithinRel(0.62F, 1e-6F));
  CHECK_THAT(f.voltage_v, WithinRel(14.1F, 1e-6F));
  REQUIRE(f.faults.size() == 1);
  CHECK(f.faults[0].kind == FaultKind::CltHigh);
  CHECK(f.raw[static_cast<std::size_t>(Channel::Oil)] == 8211);
  CHECK(f.reverse);
}

TEST_CASE("parse_frame: spaces and tabs between tokens inside the object are tolerated", "[protocol]") {
  const std::string line = seal(
      "{ \"t\" : 184523 ,\t\"fuel\": 0.62, \"oil\" :41.2,\"clt\":196.4 , \"volt\":14.1, \"faults\" : [ \"OIL_LOW\" , "
      "\"FUEL_LOW\" ] , \"raw\" : { \"fuel\" : 1 } , \"relays\" : { \"blower\" : 1 , \"ac\" : false , \"mode\" : "
      "\"floor\" , \"headlights\" : true } }");
  const Frame f = expect_frame(line);
  CHECK(f.t_ms == 184523U);
  REQUIRE(f.faults.size() == 2);
  CHECK(f.faults[1].kind == FaultKind::FuelLow);
  CHECK(f.raw[0] == 1);
  REQUIRE(f.relays.has_value());
  CHECK(f.relays->mode == HvacMode::Floor);
}

TEST_CASE("parse_frame: a trailing carriage return is tolerated", "[protocol]") {
  const std::string line = std::string(kOracleSpecLine) + "\r";
  const Frame f = expect_frame(line);
  CHECK(f.t_ms == 184523U);
}

TEST_CASE("parse_frame: only one trailing carriage return is stripped", "[protocol]") {
  expect_error(std::string(kOracleSpecLine) + "\r\r", ParseError::BadCrcField);
}

TEST_CASE("parse_frame: a trailing newline is not part of a line", "[protocol]") {
  expect_error(std::string(kOracleSpecLine) + "\n", ParseError::BadCrcField);
}

TEST_CASE("parse_frame: uppercase crc hex digits are accepted", "[protocol]") {
  const std::string object = object_of(Scalars{});  // crc f8ca: has letters to upper-case
  std::string upper = hex4(Crc16Ccitt::compute(object));
  for (char& c : upper) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  REQUIRE(upper == "F8CA");
  const Frame f = expect_frame(seal_with(object, upper));
  CHECK(f.t_ms == 184523U);
}

// ---------------------------------------------------------------------------
// Empty and too long
// ---------------------------------------------------------------------------

TEST_CASE("parse_frame: empty line", "[protocol]") {
  expect_error("", ParseError::Empty);
  expect_error("\r", ParseError::Empty);
}

TEST_CASE("parse_frame: exactly kMaxLineLength bytes is accepted", "[protocol]") {
  // Pad with an unknown string member so the sealed line lands on the limit exactly.
  const std::string probe = frame_with(R"json("pad":"")json");
  REQUIRE(probe.size() < kMaxLineLength);
  const std::string padding(kMaxLineLength - probe.size(), 'x');
  const std::string line = frame_with("\"pad\":\"" + padding + "\"");
  REQUIRE(line.size() == kMaxLineLength);
  const Frame f = expect_frame(line);
  CHECK(f.t_ms == 184523U);
}

TEST_CASE("parse_frame: kMaxLineLength bytes plus a carriage return is still accepted", "[protocol]") {
  const std::string probe = frame_with(R"json("pad":"")json");
  const std::string padding(kMaxLineLength - probe.size(), 'x');
  const std::string line = frame_with("\"pad\":\"" + padding + "\"") + "\r";
  REQUIRE(line.size() == kMaxLineLength + 1);
  expect_error(line, ParseError::None);
}

TEST_CASE("parse_frame: one byte over kMaxLineLength is TooLong before anything else is examined", "[protocol]") {
  const std::string probe = frame_with(R"json("pad":"")json");
  const std::string padding(kMaxLineLength + 1 - probe.size(), 'x');
  const std::string valid_but_long = frame_with("\"pad\":\"" + padding + "\"");
  REQUIRE(valid_but_long.size() == kMaxLineLength + 1);
  expect_error(valid_but_long, ParseError::TooLong);

  const std::string garbage(kMaxLineLength + 1, 'g');  // no crc at all, still reported as TooLong
  expect_error(garbage, ParseError::TooLong);
}

// ---------------------------------------------------------------------------
// CRC field
// ---------------------------------------------------------------------------

TEST_CASE("parse_frame: a line without a crc member is MissingCrc", "[protocol]") {
  expect_error(object_of(Scalars{}), ParseError::MissingCrc);
  expect_error("{}", ParseError::MissingCrc);
  expect_error("garbage", ParseError::MissingCrc);
  expect_error(R"json({"crc":"f8ca"})json", ParseError::MissingCrc);  // no leading comma: not the wire shape
}

TEST_CASE("parse_frame: whitespace inside the crc member is MissingCrc", "[protocol]") {
  // The crc member must appear exactly as the MCU serialises it.
  const std::string object = object_of(Scalars{});
  const std::string crc = hex4(Crc16Ccitt::compute(object));
  const std::string body = object.substr(0, object.size() - 1);
  expect_error(body + R"(, "crc":")" + crc + "\"}", ParseError::MissingCrc);
  expect_error(body + R"(,"crc": ")" + crc + "\"}", ParseError::MissingCrc);
  expect_error(body + R"(,"crc" :")" + crc + "\"}", ParseError::MissingCrc);
}

TEST_CASE("parse_frame: crc must be the last member", "[protocol]") {
  const std::string object = object_of(Scalars{});
  const std::string crc = hex4(Crc16Ccitt::compute(object));
  const std::string body = object.substr(0, object.size() - 1);
  expect_error(body + R"(,"crc":")" + crc + R"(","rev":false})", ParseError::BadCrcField);
}

TEST_CASE("parse_frame: crc digits must be exactly four hex characters", "[protocol]") {
  const std::string object = object_of(Scalars{});
  expect_error(seal_with(object, ""), ParseError::BadCrcField);
  expect_error(seal_with(object, "abc"), ParseError::BadCrcField);
  expect_error(seal_with(object, "abcde"), ParseError::BadCrcField);
  expect_error(seal_with(object, "zzzz"), ParseError::BadCrcField);
  expect_error(seal_with(object, "0x12"), ParseError::BadCrcField);
  expect_error(seal_with(object, "-123"), ParseError::BadCrcField);
  expect_error(seal_with(object, "+123"), ParseError::BadCrcField);
  expect_error(seal_with(object, "12 3"), ParseError::BadCrcField);
}

TEST_CASE("parse_frame: text after the crc member is BadCrcField", "[protocol]") {
  const std::string line = line_of(Scalars{});
  expect_error(line + "x", ParseError::BadCrcField);
  expect_error(line + "}", ParseError::BadCrcField);
  expect_error(line + " ", ParseError::BadCrcField);
}

TEST_CASE("parse_frame: a crc member without its closing brace is BadCrcField", "[protocol]") {
  const std::string line = line_of(Scalars{});
  expect_error(line.substr(0, line.size() - 1), ParseError::BadCrcField);
  expect_error(line.substr(0, line.size() - 2), ParseError::BadCrcField);
}

TEST_CASE("parse_frame: a wrong crc value is CrcMismatch", "[protocol]") {
  const std::string object = object_of(Scalars{});
  const std::uint16_t good = Crc16Ccitt::compute(object);
  expect_error(seal_with(object, hex4(static_cast<std::uint16_t>(good ^ 0x0001U))), ParseError::CrcMismatch);
  expect_error(seal_with(object, hex4(static_cast<std::uint16_t>(good ^ 0x8000U))), ParseError::CrcMismatch);
  expect_error(seal_with(object, hex4(static_cast<std::uint16_t>(~good))), ParseError::CrcMismatch);
  expect_error(seal_with(object, "0000"), good == 0 ? ParseError::None : ParseError::CrcMismatch);
}

TEST_CASE("parse_frame: the crc covers the closing brace the MCU moved after it", "[protocol]") {
  // Sealing with a crc computed over the object *without* its brace must be rejected; otherwise
  // firmware and host could silently disagree about what the checksum protects.
  const std::string object = object_of(Scalars{});
  const std::string_view without_brace(object.data(), object.size() - 1);
  const std::uint16_t wrong_convention = Crc16Ccitt::compute(without_brace);
  REQUIRE(wrong_convention != Crc16Ccitt::compute(object));
  expect_error(seal_with(object, hex4(wrong_convention)), ParseError::CrcMismatch);
}

TEST_CASE("parse_frame: every single-bit error in the body is caught as CrcMismatch", "[protocol]") {
  const std::string line = frame_with(
      R"json("raw":{"fuel":12843,"oil":8211,"clt":19004,"volt":21877},"relays":{"blower":2,"ac":true,"mode":"vent","headlights":false},"rev":false)json");
  const std::size_t body_end = line.rfind(R"(,"crc":")");
  REQUIRE(body_end != std::string::npos);

  std::size_t mismatches = 0;
  for (std::size_t i = 0; i < body_end; ++i) {
    for (int bit = 0; bit < 8; ++bit) {
      std::string corrupted = line;
      corrupted[i] = static_cast<char>(static_cast<unsigned char>(corrupted[i]) ^ (1U << bit));
      const ParseResult r = parse_frame(corrupted);
      if (r.error == ParseError::CrcMismatch && !r.frame.has_value()) {
        ++mismatches;
      } else {
        INFO("byte " << i << " bit " << bit << " produced error " << static_cast<int>(r.error));
        CHECK(r.error == ParseError::CrcMismatch);
      }
    }
  }
  CHECK(mismatches == body_end * 8);
}

TEST_CASE("parse_frame: a corrupt body is reported as CrcMismatch, never parsed", "[protocol]") {
  // Acceptance checklist: CRC failures are counted and dropped, never parsed. A body that is
  // both malformed and mis-summed must come back as the crc error, not the parse error.
  constexpr std::string_view broken =
      R"json({"t":184523,"fuel":0.62,"oil":41.2 "clt":196.4,"volt":14.1,"faults":[]})json";
  expect_error(seal_with(broken, "0000"), ParseError::CrcMismatch);
  expect_error(seal(broken), ParseError::Malformed);
}

TEST_CASE("parse_frame: swapped bytes in the body are CrcMismatch", "[protocol]") {
  std::string line = line_of(Scalars{});
  const std::size_t a = line.find("184523");
  REQUIRE(a != std::string::npos);
  std::swap(line[a], line[a + 1]);  // "184523" -> "814523": same bytes, different order
  expect_error(line, ParseError::CrcMismatch);
}

// ---------------------------------------------------------------------------
// Required fields
// ---------------------------------------------------------------------------

TEST_CASE("parse_frame: each required member missing is MissingField", "[protocol]") {
  for (const std::string_view key : {"t", "fuel", "oil", "clt", "volt", "faults"}) {
    INFO("omitted: " << key);
    expect_error(frame_without(key), ParseError::MissingField);
  }
}

TEST_CASE("parse_frame: unknown members do not satisfy required ones", "[protocol]") {
  expect_error(seal(R"json({"fuel":0.62,"oil":41.2,"clt":196.4,"volt":14.1,"faults":[],"time":184523})json"),
               ParseError::MissingField);
}

TEST_CASE("parse_frame: an object with no members is Malformed", "[protocol]") {
  expect_error(seal("{}"), ParseError::Malformed);
}

// ---------------------------------------------------------------------------
// t
// ---------------------------------------------------------------------------

TEST_CASE("parse_frame: t accepts the full uint32 range", "[protocol]") {
  CHECK(expect_frame(line_of(Scalars{.t = "0"})).t_ms == 0U);
  CHECK(expect_frame(line_of(Scalars{.t = "4294967295"})).t_ms == 4294967295U);
}

TEST_CASE("parse_frame: t rejects values that are not a uint32", "[protocol]") {
  expect_error(line_of(Scalars{.t = "4294967296"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.t = "-1"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.t = "184523.0"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.t = "1e3"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.t = "\"184523\""}), ParseError::Malformed);
  expect_error(line_of(Scalars{.t = "true"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.t = "null"}), ParseError::Malformed);
}

// ---------------------------------------------------------------------------
// Scalar measurements: format
// ---------------------------------------------------------------------------

TEST_CASE("parse_frame: measurements accept integer, decimal, scientific and signed tokens", "[protocol]") {
  CHECK(expect_frame(line_of(Scalars{.oil = "40"})).oil_psi == 40.0F);
  CHECK_THAT(expect_frame(line_of(Scalars{.fuel = "6.2e-1"})).fuel_fraction, WithinRel(0.62F, 1e-6F));
  CHECK_THAT(expect_frame(line_of(Scalars{.clt = "1.964E2"})).coolant_f, WithinRel(196.4F, 1e-6F));
  CHECK(expect_frame(line_of(Scalars{.clt = "-12.5"})).coolant_f == -12.5F);
  CHECK(expect_frame(line_of(Scalars{.volt = "+12"})).voltage_v == 12.0F);
  CHECK(expect_frame(line_of(Scalars{.fuel = ".5"})).fuel_fraction == 0.5F);
}

TEST_CASE("parse_frame: measurements reject non-numeric tokens", "[protocol]") {
  expect_error(line_of(Scalars{.fuel = "\"0.62\""}), ParseError::Malformed);
  expect_error(line_of(Scalars{.fuel = "true"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.fuel = "null"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.fuel = "[0.62]"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.fuel = "{}"}), ParseError::Malformed);
}

TEST_CASE("parse_frame: measurements reject broken number tokens", "[protocol]") {
  expect_error(line_of(Scalars{.fuel = "0.6.2"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.fuel = "1e"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.fuel = "-"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.fuel = "."}), ParseError::Malformed);
  expect_error(line_of(Scalars{.fuel = "0.62x"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.fuel = "0x1p-1"}), ParseError::Malformed);
}

TEST_CASE("parse_frame: measurements reject an empty value", "[protocol]") {
  expect_error(seal(R"json({"t":1,"fuel":,"oil":40,"clt":180,"volt":14,"faults":[]})json"), ParseError::Malformed);
  expect_error(seal(R"json({"t":1,"fuel":0.5,"oil":40,"clt":180,"volt":14,"faults":[],"volt":})json"),
               ParseError::Malformed);
}

TEST_CASE("parse_frame: measurements reject NaN and infinity spellings", "[protocol]") {
  expect_error(line_of(Scalars{.oil = "nan"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.oil = "NaN"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.oil = "inf"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.oil = "-inf"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.oil = "Infinity"}), ParseError::Malformed);
}

TEST_CASE("parse_frame: measurements reject magnitudes that overflow a float", "[protocol]") {
  expect_error(line_of(Scalars{.oil = "1e999"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.oil = "1e39"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.clt = "-1e39"}), ParseError::Malformed);
}

// ---------------------------------------------------------------------------
// Scalar measurements: plausibility limits (spec §4.1, not warning thresholds)
// ---------------------------------------------------------------------------

TEST_CASE("parse_frame: fuel is accepted on 0 to 1 inclusive", "[protocol]") {
  CHECK(expect_frame(line_of(Scalars{.fuel = "0"})).fuel_fraction == 0.0F);
  CHECK(expect_frame(line_of(Scalars{.fuel = "0.0"})).fuel_fraction == 0.0F);
  CHECK(expect_frame(line_of(Scalars{.fuel = "1"})).fuel_fraction == 1.0F);
  CHECK(expect_frame(line_of(Scalars{.fuel = "1.0"})).fuel_fraction == 1.0F);
  CHECK(expect_frame(line_of(Scalars{.fuel = "0.5"})).fuel_fraction == 0.5F);
}

TEST_CASE("parse_frame: fuel outside 0 to 1 is OutOfRange", "[protocol]") {
  expect_error(line_of(Scalars{.fuel = "-0.001"}), ParseError::OutOfRange);
  expect_error(line_of(Scalars{.fuel = "1.001"}), ParseError::OutOfRange);
  expect_error(line_of(Scalars{.fuel = "62"}), ParseError::OutOfRange);  // percent, not fraction
  expect_error(line_of(Scalars{.fuel = "-1"}), ParseError::OutOfRange);
}

TEST_CASE("parse_frame: oil is accepted on 0 to 200 psi inclusive", "[protocol]") {
  CHECK(expect_frame(line_of(Scalars{.oil = "0"})).oil_psi == 0.0F);
  CHECK(expect_frame(line_of(Scalars{.oil = "200"})).oil_psi == 200.0F);
  CHECK(expect_frame(line_of(Scalars{.oil = "200.0"})).oil_psi == 200.0F);
}

TEST_CASE("parse_frame: oil outside 0 to 200 psi is OutOfRange", "[protocol]") {
  expect_error(line_of(Scalars{.oil = "-0.01"}), ParseError::OutOfRange);
  expect_error(line_of(Scalars{.oil = "200.01"}), ParseError::OutOfRange);
  expect_error(line_of(Scalars{.oil = "1000"}), ParseError::OutOfRange);
}

TEST_CASE("parse_frame: coolant is accepted on -40 to 400 F inclusive", "[protocol]") {
  CHECK(expect_frame(line_of(Scalars{.clt = "-40"})).coolant_f == -40.0F);
  CHECK(expect_frame(line_of(Scalars{.clt = "400"})).coolant_f == 400.0F);
  CHECK(expect_frame(line_of(Scalars{.clt = "0"})).coolant_f == 0.0F);
}

TEST_CASE("parse_frame: coolant outside -40 to 400 F is OutOfRange", "[protocol]") {
  expect_error(line_of(Scalars{.clt = "-40.5"}), ParseError::OutOfRange);
  expect_error(line_of(Scalars{.clt = "400.5"}), ParseError::OutOfRange);
  expect_error(line_of(Scalars{.clt = "-100"}), ParseError::OutOfRange);
}

TEST_CASE("parse_frame: volt is accepted on 0 to 40 V inclusive", "[protocol]") {
  CHECK(expect_frame(line_of(Scalars{.volt = "0"})).voltage_v == 0.0F);
  CHECK(expect_frame(line_of(Scalars{.volt = "40"})).voltage_v == 40.0F);
  CHECK(expect_frame(line_of(Scalars{.volt = "12.6"})).voltage_v == 12.6F);
}

TEST_CASE("parse_frame: volt outside 0 to 40 V is OutOfRange", "[protocol]") {
  expect_error(line_of(Scalars{.volt = "-0.1"}), ParseError::OutOfRange);
  expect_error(line_of(Scalars{.volt = "40.1"}), ParseError::OutOfRange);
  expect_error(line_of(Scalars{.volt = "120"}), ParseError::OutOfRange);
}

TEST_CASE("parse_frame: values that would trip a warning are still in range for the host", "[protocol]") {
  // The MCU owns warnings (invariant 1); the host must not reject a frame merely because a
  // value is alarming.
  const Frame f = expect_frame(line_of(Scalars{.fuel = "0.02", .oil = "3", .clt = "260", .volt = "9.5"}));
  CHECK_THAT(f.oil_psi, WithinRel(3.0F, 1e-6F));
  CHECK_THAT(f.coolant_f, WithinRel(260.0F, 1e-6F));
}

// ---------------------------------------------------------------------------
// faults
// ---------------------------------------------------------------------------

TEST_CASE("parse_frame: every simple fault code is recognised", "[protocol]") {
  struct Case {
    std::string_view faults;
    FaultKind kind;
  };
  constexpr std::array<Case, 5> cases{{
      {R"(["OIL_LOW"])", FaultKind::OilLow},
      {R"(["CLT_HIGH"])", FaultKind::CltHigh},
      {R"(["FUEL_LOW"])", FaultKind::FuelLow},
      {R"(["VOLT_LOW"])", FaultKind::VoltLow},
      {R"(["VOLT_HIGH"])", FaultKind::VoltHigh},
  }};
  for (const auto& c : cases) {
    const Frame f = expect_frame(line_of(Scalars{.faults = c.faults}));
    REQUIRE(f.faults.size() == 1);
    CHECK(f.faults[0].kind == c.kind);
  }
}

TEST_CASE("parse_frame: sender faults carry their channel", "[protocol]") {
  // Channel suffixes are spelled by to_string(Channel) in frame.hpp, so this test follows
  // whatever spelling firmware and host agree on.
  for (const auto kind : {FaultKind::SenderOpen, FaultKind::SenderShort}) {
    for (std::size_t c = 0; c < c3::telemetry::kChannelCount; ++c) {
      const auto channel = static_cast<Channel>(c);
      std::string faults = "[\"";
      faults += to_string(kind);
      faults += "_";
      faults += to_string(channel);
      faults += "\"]";
      const Frame f = expect_frame(line_of(Scalars{.faults = faults}));
      REQUIRE(f.faults.size() == 1);
      CHECK(f.faults[0].kind == kind);
      CHECK(f.faults[0].channel == channel);
    }
  }
}

TEST_CASE("parse_frame: several faults keep wire order", "[protocol]") {
  const Frame f = expect_frame(line_of(Scalars{.faults = R"(["VOLT_LOW","OIL_LOW","SENDER_OPEN_FUEL"])"}));
  REQUIRE(f.faults.size() == 3);
  CHECK(f.faults[0].kind == FaultKind::VoltLow);
  CHECK(f.faults[1].kind == FaultKind::OilLow);
  CHECK(f.faults[2].kind == FaultKind::SenderOpen);
  CHECK(f.faults[2].channel == Channel::Fuel);
}

TEST_CASE("parse_frame: eight faults fill the frame", "[protocol]") {
  const Frame f = expect_frame(line_of(Scalars{
      .faults =
          R"(["OIL_LOW","CLT_HIGH","FUEL_LOW","VOLT_LOW","VOLT_HIGH","SENDER_OPEN_FUEL","SENDER_OPEN_OIL","SENDER_SHORT_VOLT"])"}));
  REQUIRE(f.faults.size() == 8);
  CHECK(f.faults.full());
  CHECK(f.faults[7].kind == FaultKind::SenderShort);
  CHECK(f.faults[7].channel == Channel::Volt);
}

TEST_CASE("parse_frame: nine faults is TooManyFaults", "[protocol]") {
  expect_error(
      line_of(Scalars{
          .faults =
              R"(["OIL_LOW","CLT_HIGH","FUEL_LOW","VOLT_LOW","VOLT_HIGH","SENDER_OPEN_FUEL","SENDER_OPEN_OIL","SENDER_SHORT_VOLT","SENDER_SHORT_OIL"])"}),
      ParseError::TooManyFaults);
}

TEST_CASE("parse_frame: an unrecognised fault code is UnknownFault", "[protocol]") {
  expect_error(line_of(Scalars{.faults = R"(["BRAKE_LOW"])"}), ParseError::UnknownFault);
  expect_error(line_of(Scalars{.faults = R"(["oil_low"])"}), ParseError::UnknownFault);
  expect_error(line_of(Scalars{.faults = R"(["OIL_LOW "])"}), ParseError::UnknownFault);
  expect_error(line_of(Scalars{.faults = R"([""])"}), ParseError::UnknownFault);
  expect_error(line_of(Scalars{.faults = R"(["SENDER_OPEN"])"}), ParseError::UnknownFault);
  expect_error(line_of(Scalars{.faults = R"(["SENDER_OPEN_"])"}), ParseError::UnknownFault);
  expect_error(line_of(Scalars{.faults = R"(["SENDER_OPEN_BRAKE"])"}), ParseError::UnknownFault);
  expect_error(line_of(Scalars{.faults = R"(["OIL_LOW","BRAKE_LOW"])"}), ParseError::UnknownFault);
}

TEST_CASE("parse_frame: faults must be an array of strings", "[protocol]") {
  expect_error(line_of(Scalars{.faults = R"("OIL_LOW")"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.faults = "{}"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.faults = "null"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.faults = "0"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.faults = "[OIL_LOW]"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.faults = "[1]"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.faults = R"([["OIL_LOW"]])"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.faults = R"(["OIL_LOW",])"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.faults = R"(["OIL_LOW" "CLT_HIGH"])"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.faults = R"(["OIL_LOW")"}), ParseError::Malformed);
  expect_error(line_of(Scalars{.faults = R"(["OIL\_LOW"])"}), ParseError::Malformed);
}

// ---------------------------------------------------------------------------
// raw
// ---------------------------------------------------------------------------

TEST_CASE("parse_frame: raw counts land on their channels", "[protocol]") {
  const Frame f = expect_frame(frame_with(R"json("raw":{"fuel":12843,"oil":8211,"clt":19004,"volt":21877})json"));
  CHECK(f.raw[static_cast<std::size_t>(Channel::Fuel)] == 12843);
  CHECK(f.raw[static_cast<std::size_t>(Channel::Oil)] == 8211);
  CHECK(f.raw[static_cast<std::size_t>(Channel::Coolant)] == 19004);
  CHECK(f.raw[static_cast<std::size_t>(Channel::Volt)] == 21877);
}

TEST_CASE("parse_frame: raw accepts negative differential counts and int32 extremes", "[protocol]") {
  const Frame f =
      expect_frame(frame_with(R"json("raw":{"fuel":-32768,"oil":2147483647,"clt":-2147483648,"volt":0})json"));
  CHECK(f.raw[0] == -32768);
  CHECK(f.raw[1] == 2147483647);
  CHECK(f.raw[2] == -2147483647 - 1);
  CHECK(f.raw[3] == 0);
}

TEST_CASE("parse_frame: raw may be partial or empty; missing channels stay 0", "[protocol]") {
  const Frame partial = expect_frame(frame_with(R"json("raw":{"oil":8211})json"));
  CHECK(partial.raw[0] == 0);
  CHECK(partial.raw[1] == 8211);
  CHECK(partial.raw[2] == 0);
  CHECK(partial.raw[3] == 0);

  const Frame empty = expect_frame(frame_with(R"json("raw":{})json"));
  for (const auto raw : empty.raw) {
    CHECK(raw == 0);
  }
}

TEST_CASE("parse_frame: unknown raw keys are skipped", "[protocol]") {
  const Frame f = expect_frame(frame_with(R"json("raw":{"fuel":1,"aux":99,"volt":4})json"));
  CHECK(f.raw[0] == 1);
  CHECK(f.raw[3] == 4);
}

TEST_CASE("parse_frame: raw values must be integers", "[protocol]") {
  expect_error(frame_with(R"json("raw":{"fuel":12843.5})json"), ParseError::Malformed);
  expect_error(frame_with(R"json("raw":{"fuel":"12843"})json"), ParseError::Malformed);
  expect_error(frame_with(R"json("raw":{"fuel":true})json"), ParseError::Malformed);
  expect_error(frame_with(R"json("raw":{"fuel":1e3})json"), ParseError::Malformed);
  expect_error(frame_with(R"json("raw":{"fuel":2147483648})json"), ParseError::Malformed);
}

TEST_CASE("parse_frame: raw must be an object", "[protocol]") {
  expect_error(frame_with(R"json("raw":[1,2,3,4])json"), ParseError::Malformed);
  expect_error(frame_with(R"json("raw":1)json"), ParseError::Malformed);
  expect_error(frame_with(R"json("raw":null)json"), ParseError::Malformed);
  expect_error(frame_with(R"json("raw":{"fuel":1,})json"), ParseError::Malformed);
  expect_error(frame_with(R"json("raw":{"fuel":1 "oil":2})json"), ParseError::Malformed);
  expect_error(frame_with(R"json("raw":{"fuel" 1})json"), ParseError::Malformed);
  expect_error(frame_with(R"json("raw":{fuel:1})json"), ParseError::Malformed);
}

// ---------------------------------------------------------------------------
// relays
// ---------------------------------------------------------------------------

TEST_CASE("parse_frame: relays with only the established outputs leaves doors and high unknown", "[protocol]") {
  const Frame f =
      expect_frame(frame_with(R"json("relays":{"blower":3,"ac":false,"mode":"defrost","headlights":true})json"));
  REQUIRE(f.relays.has_value());
  CHECK(f.relays->blower == HvacBlower::L3);
  CHECK(f.relays->ac_enabled == false);
  CHECK(f.relays->mode == HvacMode::Defrost);
  CHECK(f.relays->headlights_on == true);
  CHECK_FALSE(f.relays->lamp_doors_open.has_value());
  CHECK_FALSE(f.relays->highbeams_on.has_value());
}

TEST_CASE("parse_frame: relays doors and high are carried when present", "[protocol]") {
  const Frame f = expect_frame(frame_with(
      R"json("relays":{"blower":0,"ac":false,"mode":"vent","headlights":true,"doors":true,"high":true})json"));
  REQUIRE(f.relays.has_value());
  REQUIRE(f.relays->lamp_doors_open.has_value());
  CHECK(*f.relays->lamp_doors_open);
  REQUIRE(f.relays->highbeams_on.has_value());
  CHECK(*f.relays->highbeams_on);

  const Frame doors_only = expect_frame(
      frame_with(R"json("relays":{"blower":0,"ac":false,"mode":"vent","headlights":false,"doors":false})json"));
  REQUIRE(doors_only.relays->lamp_doors_open.has_value());
  CHECK_FALSE(*doors_only.relays->lamp_doors_open);
  CHECK_FALSE(doors_only.relays->highbeams_on.has_value());
}

TEST_CASE("parse_frame: relays blower maps 0 to 4 onto HvacBlower", "[protocol]") {
  constexpr std::array<HvacBlower, 5> kExpected{HvacBlower::Off, HvacBlower::L1, HvacBlower::L2, HvacBlower::L3,
                                                HvacBlower::L4};
  for (std::size_t i = 0; i < kExpected.size(); ++i) {
    std::string relays = R"json("relays":{"blower":)json";
    relays += std::to_string(i);
    relays += R"json(,"ac":false,"mode":"vent","headlights":false})json";
    const Frame f = expect_frame(frame_with(relays));
    REQUIRE(f.relays.has_value());
    CHECK(f.relays->blower == kExpected[i]);
  }
}

TEST_CASE("parse_frame: relays blower above 4 is OutOfRange", "[protocol]") {
  expect_error(frame_with(R"json("relays":{"blower":5,"ac":false,"mode":"vent","headlights":false})json"),
               ParseError::OutOfRange);
  expect_error(frame_with(R"json("relays":{"blower":255,"ac":false,"mode":"vent","headlights":false})json"),
               ParseError::OutOfRange);
}

TEST_CASE("parse_frame: relays blower that is not a small non-negative integer is rejected", "[protocol]") {
  for (const std::string_view blower : {"-1", "2.0", "\"2\"", "true", "null"}) {
    std::string relays = R"json("relays":{"blower":)json";
    relays += blower;
    relays += R"json(,"ac":false,"mode":"vent","headlights":false})json";
    INFO("blower: " << blower);
    const ParseResult r = parse_frame(frame_with(relays));
    CHECK_FALSE(r.frame.has_value());
    CHECK(r.error != ParseError::None);
  }
}

TEST_CASE("parse_frame: relays mode accepts the three HVAC modes", "[protocol]") {
  struct Case {
    std::string_view mode;
    HvacMode expected;
  };
  constexpr std::array<Case, 3> cases{
      {{"vent", HvacMode::Vent}, {"floor", HvacMode::Floor}, {"defrost", HvacMode::Defrost}}};
  for (const auto& c : cases) {
    std::string relays = R"json("relays":{"blower":1,"ac":true,"mode":")json";
    relays += c.mode;
    relays += R"json(","headlights":false})json";
    const Frame f = expect_frame(frame_with(relays));
    REQUIRE(f.relays.has_value());
    CHECK(f.relays->mode == c.expected);
  }
}

TEST_CASE("parse_frame: relays mode rejects unknown names and non-strings", "[protocol]") {
  expect_error(frame_with(R"json("relays":{"blower":1,"ac":true,"mode":"auto","headlights":false})json"),
               ParseError::OutOfRange);
  expect_error(frame_with(R"json("relays":{"blower":1,"ac":true,"mode":"Vent","headlights":false})json"),
               ParseError::OutOfRange);
  expect_error(frame_with(R"json("relays":{"blower":1,"ac":true,"mode":"","headlights":false})json"),
               ParseError::OutOfRange);
  expect_error(frame_with(R"json("relays":{"blower":1,"ac":true,"mode":1,"headlights":false})json"),
               ParseError::Malformed);
  expect_error(frame_with(R"json("relays":{"blower":1,"ac":true,"mode":null,"headlights":false})json"),
               ParseError::Malformed);
}

TEST_CASE("parse_frame: relays booleans must be true or false literals", "[protocol]") {
  expect_error(frame_with(R"json("relays":{"blower":1,"ac":"true","mode":"vent","headlights":false})json"),
               ParseError::Malformed);
  expect_error(frame_with(R"json("relays":{"blower":1,"ac":1,"mode":"vent","headlights":false})json"),
               ParseError::Malformed);
  expect_error(frame_with(R"json("relays":{"blower":1,"ac":true,"mode":"vent","headlights":0})json"),
               ParseError::Malformed);
  expect_error(frame_with(R"json("relays":{"blower":1,"ac":true,"mode":"vent","headlights":false,"doors":"no"})json"),
               ParseError::Malformed);
  expect_error(frame_with(R"json("relays":{"blower":1,"ac":true,"mode":"vent","headlights":false,"high":null})json"),
               ParseError::Malformed);
  expect_error(frame_with(R"json("relays":{"blower":1,"ac":True,"mode":"vent","headlights":false})json"),
               ParseError::Malformed);
}

TEST_CASE("parse_frame: relays missing any established output is MissingField", "[protocol]") {
  expect_error(frame_with(R"json("relays":{})json"), ParseError::MissingField);
  expect_error(frame_with(R"json("relays":{"ac":true,"mode":"vent","headlights":false})json"),
               ParseError::MissingField);
  expect_error(frame_with(R"json("relays":{"blower":1,"mode":"vent","headlights":false})json"),
               ParseError::MissingField);
  expect_error(frame_with(R"json("relays":{"blower":1,"ac":true,"headlights":false})json"), ParseError::MissingField);
  expect_error(frame_with(R"json("relays":{"blower":1,"ac":true,"mode":"vent"})json"), ParseError::MissingField);
  expect_error(frame_with(R"json("relays":{"blower":1,"ac":true,"mode":"vent","doors":true,"high":false})json"),
               ParseError::MissingField);
}

TEST_CASE("parse_frame: relays duplicate members are Malformed", "[protocol]") {
  expect_error(frame_with(R"json("relays":{"blower":1,"blower":2,"ac":true,"mode":"vent","headlights":false})json"),
               ParseError::Malformed);
  expect_error(frame_with(R"json("relays":{"blower":1,"ac":true,"ac":true,"mode":"vent","headlights":false})json"),
               ParseError::Malformed);
  expect_error(frame_with(R"json("relays":{"blower":1,"ac":true,"mode":"vent","mode":"vent","headlights":false})json"),
               ParseError::Malformed);
  expect_error(
      frame_with(R"json("relays":{"blower":1,"ac":true,"mode":"vent","headlights":false,"headlights":false})json"),
      ParseError::Malformed);
  expect_error(
      frame_with(
          R"json("relays":{"blower":1,"ac":true,"mode":"vent","headlights":false,"doors":true,"doors":true})json"),
      ParseError::Malformed);
}

TEST_CASE("parse_frame: relays unknown members are skipped", "[protocol]") {
  const Frame f = expect_frame(frame_with(
      R"json("relays":{"blower":1,"compressor":true,"ac":true,"mode":"vent","headlights":false,"meta":{"a":[1,2]}})json"));
  REQUIRE(f.relays.has_value());
  CHECK(f.relays->blower == HvacBlower::L1);
  CHECK(f.relays->ac_enabled);
}

TEST_CASE("parse_frame: relays must be an object", "[protocol]") {
  expect_error(frame_with(R"json("relays":1)json"), ParseError::Malformed);
  expect_error(frame_with(R"json("relays":"vent")json"), ParseError::Malformed);
  expect_error(frame_with(R"json("relays":[])json"), ParseError::Malformed);
  expect_error(frame_with(R"json("relays":null)json"), ParseError::Malformed);
  expect_error(frame_with(R"json("relays":{"blower":1,"ac":true,"mode":"vent","headlights":false)json"),
               ParseError::Malformed);
  expect_error(frame_with(R"json("relays":{"blower":1,"ac":true,"mode":"vent","headlights":false,})json"),
               ParseError::Malformed);
}

// ---------------------------------------------------------------------------
// rev
// ---------------------------------------------------------------------------

TEST_CASE("parse_frame: rev is carried as a boolean", "[protocol]") {
  CHECK(expect_frame(frame_with(R"json("rev":true)json")).reverse == true);
  CHECK(expect_frame(frame_with(R"json("rev":false)json")).reverse == false);
}

TEST_CASE("parse_frame: rev must be a boolean literal", "[protocol]") {
  expect_error(frame_with(R"json("rev":1)json"), ParseError::Malformed);
  expect_error(frame_with(R"json("rev":"true")json"), ParseError::Malformed);
  expect_error(frame_with(R"json("rev":null)json"), ParseError::Malformed);
  expect_error(frame_with(R"json("rev":TRUE)json"), ParseError::Malformed);
}

// ---------------------------------------------------------------------------
// Unknown members (forward compatibility)
// ---------------------------------------------------------------------------

TEST_CASE("parse_frame: unknown scalar members are skipped", "[protocol]") {
  CHECK(expect_frame(frame_with(R"json("fw":3)json")).t_ms == 184523U);
  CHECK(expect_frame(frame_with(R"json("fw":"3.1.0")json")).t_ms == 184523U);
  CHECK(expect_frame(frame_with(R"json("fw":-1.5e3)json")).t_ms == 184523U);
  CHECK(expect_frame(frame_with(R"json("ok":true)json")).t_ms == 184523U);
  CHECK(expect_frame(frame_with(R"json("ok":false)json")).t_ms == 184523U);
  CHECK(expect_frame(frame_with(R"json("ok":null)json")).t_ms == 184523U);
  CHECK(expect_frame(frame_with(R"json("s":"")json")).t_ms == 184523U);
}

TEST_CASE("parse_frame: unknown container members are skipped", "[protocol]") {
  CHECK(expect_frame(frame_with(R"json("a":[1,2,3])json")).t_ms == 184523U);
  CHECK(expect_frame(frame_with(R"json("a":["x","y"])json")).t_ms == 184523U);
  CHECK(expect_frame(frame_with(R"json("o":{"k":1,"s":"v","b":true})json")).t_ms == 184523U);
  CHECK(expect_frame(frame_with(R"json("o":{"k":{"n":[1,{"deep":true}]}})json")).t_ms == 184523U);
}

TEST_CASE("parse_frame: unknown empty containers are skipped at any depth", "[protocol]") {
  CHECK(expect_frame(frame_with(R"json("o":{})json")).t_ms == 184523U);
  CHECK(expect_frame(frame_with(R"json("a":[])json")).t_ms == 184523U);
  CHECK(expect_frame(frame_with(R"json("o":{"x":{}})json")).t_ms == 184523U);
  CHECK(expect_frame(frame_with(R"json("o":{"x":[]})json")).t_ms == 184523U);
  CHECK(expect_frame(frame_with(R"json("a":[[]])json")).t_ms == 184523U);
  CHECK(expect_frame(frame_with(R"json("a":[{}])json")).t_ms == 184523U);
  CHECK(expect_frame(frame_with(R"json("o":{"a":[],"b":{},"c":1})json")).t_ms == 184523U);
  CHECK(expect_frame(frame_with(R"json("a":[{},[],1,{}])json")).t_ms == 184523U);
}

TEST_CASE("parse_frame: unknown members may appear before, between and after known ones", "[protocol]") {
  const Frame f = expect_frame(seal(
      R"json({"x":{"y":[]},"t":184523,"fuel":0.62,"z":null,"oil":41.2,"clt":196.4,"volt":14.1,"faults":[],"w":"end"})json"));
  CHECK(f.t_ms == 184523U);
  CHECK(f.faults.empty());
}

TEST_CASE("parse_frame: unknown members nest up to five levels and no further", "[protocol]") {
  CHECK(expect_frame(frame_with(R"json("a":[[[[[1]]]]])json")).t_ms == 184523U);
  CHECK(expect_frame(frame_with(R"json("a":{"b":{"c":{"d":{"e":1}}}})json")).t_ms == 184523U);
  expect_error(frame_with(R"json("a":[[[[[[1]]]]]])json"), ParseError::Malformed);
  expect_error(frame_with(R"json("a":{"b":{"c":{"d":{"e":{"f":{"g":1}}}}}})json"), ParseError::Malformed);
}

TEST_CASE("parse_frame: broken unknown members are Malformed", "[protocol]") {
  expect_error(frame_with(R"json("a":[1,2)json"), ParseError::Malformed);
  expect_error(frame_with(R"json("a":{"k":1)json"), ParseError::Malformed);
  expect_error(frame_with(R"json("a":{"k"})json"), ParseError::Malformed);
  expect_error(frame_with(R"json("a":{k:1})json"), ParseError::Malformed);
  expect_error(frame_with(R"json("a":[1,])json"), ParseError::Malformed);
  expect_error(frame_with(R"json("a":[1 2])json"), ParseError::Malformed);
  expect_error(frame_with(R"json("a":"esc\"aped")json"), ParseError::Malformed);
  expect_error(frame_with(R"json("a":tru)json"), ParseError::Malformed);
  expect_error(frame_with(R"json("a":nul)json"), ParseError::Malformed);
  expect_error(frame_with(R"json("a":)json"), ParseError::Malformed);
  expect_error(frame_with(R"json("a":}")json"), ParseError::Malformed);
}

// ---------------------------------------------------------------------------
// Structure
// ---------------------------------------------------------------------------

TEST_CASE("parse_frame: the body must be an object", "[protocol]") {
  expect_error(seal("x" + object_of(Scalars{})), ParseError::Malformed);
  expect_error(seal("[1,2}"), ParseError::Malformed);
  expect_error(seal("1}"), ParseError::Malformed);
  expect_error(seal("\"s\"}"), ParseError::Malformed);
}

TEST_CASE("parse_frame: keys must be quoted strings followed by a colon", "[protocol]") {
  expect_error(seal(R"json({t:184523,"fuel":0.62,"oil":41.2,"clt":196.4,"volt":14.1,"faults":[]})json"),
               ParseError::Malformed);
  expect_error(seal(R"json({"t" 184523,"fuel":0.62,"oil":41.2,"clt":196.4,"volt":14.1,"faults":[]})json"),
               ParseError::Malformed);
  expect_error(seal(R"json({"t"=184523,"fuel":0.62,"oil":41.2,"clt":196.4,"volt":14.1,"faults":[]})json"),
               ParseError::Malformed);
  expect_error(seal(R"json({"t\n":184523,"fuel":0.62,"oil":41.2,"clt":196.4,"volt":14.1,"faults":[]})json"),
               ParseError::Malformed);
  expect_error(seal(R"json({184523,"fuel":0.62,"oil":41.2,"clt":196.4,"volt":14.1,"faults":[]})json"),
               ParseError::Malformed);
}

TEST_CASE("parse_frame: members must be separated by exactly one comma", "[protocol]") {
  expect_error(seal(R"json({"t":184523 "fuel":0.62,"oil":41.2,"clt":196.4,"volt":14.1,"faults":[]})json"),
               ParseError::Malformed);
  expect_error(seal(R"json({"t":184523,,"fuel":0.62,"oil":41.2,"clt":196.4,"volt":14.1,"faults":[]})json"),
               ParseError::Malformed);
  expect_error(seal(R"json({"t":184523;"fuel":0.62,"oil":41.2,"clt":196.4,"volt":14.1,"faults":[]})json"),
               ParseError::Malformed);
}

TEST_CASE("parse_frame: a trailing comma before the crc is Malformed", "[protocol]") {
  expect_error(seal(R"json({"t":184523,"fuel":0.62,"oil":41.2,"clt":196.4,"volt":14.1,"faults":[],})json"),
               ParseError::Malformed);
}

TEST_CASE("parse_frame: a stray closing brace inside the body is Malformed", "[protocol]") {
  expect_error(seal(R"json({"t":184523,"fuel":0.62,"oil":41.2,"clt":196.4,"volt":14.1,"faults":[]}})json"),
               ParseError::Malformed);
  expect_error(seal(R"json({"t":184523},"fuel":0.62,"oil":41.2,"clt":196.4,"volt":14.1,"faults":[]})json"),
               ParseError::Malformed);
}

TEST_CASE("parse_frame: the first error in wire order wins", "[protocol]") {
  // fuel is out of range and faults is unknown: fuel comes first on the wire.
  expect_error(line_of(Scalars{.fuel = "2", .faults = R"(["NOPE"])"}), ParseError::OutOfRange);
  // faults is malformed and volt is out of range: volt comes first on the wire.
  expect_error(line_of(Scalars{.volt = "99", .faults = "[1]"}), ParseError::OutOfRange);
  // the body is malformed at raw, but a required field is also missing: malformed is hit first.
  expect_error(seal(R"json({"t":1,"fuel":0.5,"oil":40,"clt":180,"raw":[]})json"), ParseError::Malformed);
}

TEST_CASE("parse_frame: a rejected frame yields no partial data", "[protocol]") {
  const ParseResult r = parse_frame(frame_with(R"json("raw":{"fuel":1},"rev":true,"bad":[)json"));
  CHECK(r.error == ParseError::Malformed);
  CHECK_FALSE(r.frame.has_value());
}

TEST_CASE("parse_frame: is deterministic and side-effect free across calls", "[protocol]") {
  const std::string good = line_of(Scalars{});
  const std::string bad = seal_with(object_of(Scalars{}), "0000");
  for (int i = 0; i < 3; ++i) {
    expect_error(bad, ParseError::CrcMismatch);
    CHECK(expect_frame(good).t_ms == 184523U);
    expect_error("", ParseError::Empty);
  }
}

// ---------------------------------------------------------------------------
// LineReader
// ---------------------------------------------------------------------------

TEST_CASE("LineReader: delivers a complete line without its newline", "[protocol]") {
  LineReader reader;
  std::vector<std::string> lines;
  feed(reader, "abc\n", lines);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == "abc");
  CHECK(reader.pending() == 0);
  CHECK(reader.dropped() == 0);
}

TEST_CASE("LineReader: buffers a partial line until the newline arrives", "[protocol]") {
  LineReader reader;
  std::vector<std::string> lines;
  feed(reader, "abc", lines);
  CHECK(lines.empty());
  CHECK(reader.pending() == 3);
  feed(reader, "def", lines);
  CHECK(lines.empty());
  CHECK(reader.pending() == 6);
  feed(reader, "\n", lines);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == "abcdef");
  CHECK(reader.pending() == 0);
}

TEST_CASE("LineReader: reassembles a frame fed one byte at a time", "[protocol]") {
  LineReader reader;
  std::vector<std::string> lines;
  const std::string stream = std::string(kOracleSpecLine) + "\n";
  for (const char c : stream) {
    feed(reader, std::string_view(&c, 1), lines);
  }
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == kOracleSpecLine);
  CHECK(parse_frame(lines[0]).error == ParseError::None);
}

TEST_CASE("LineReader: splits several lines from one chunk", "[protocol]") {
  LineReader reader;
  std::vector<std::string> lines;
  feed(reader, "one\ntwo\nthree\n", lines);
  REQUIRE(lines.size() == 3);
  CHECK(lines[0] == "one");
  CHECK(lines[1] == "two");
  CHECK(lines[2] == "three");
}

TEST_CASE("LineReader: a chunk boundary in the middle of a line does not split it", "[protocol]") {
  LineReader reader;
  std::vector<std::string> lines;
  feed(reader, "one\ntw", lines);
  feed(reader, "o\nthr", lines);
  feed(reader, "ee\n", lines);
  REQUIRE(lines.size() == 3);
  CHECK(lines[1] == "two");
  CHECK(lines[2] == "three");
}

TEST_CASE("LineReader: keeps a carriage return for parse_frame to strip", "[protocol]") {
  LineReader reader;
  std::vector<std::string> lines;
  feed(reader, std::string(kOracleSpecLine) + "\r\n", lines);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].back() == '\r');
  CHECK(parse_frame(lines[0]).error == ParseError::None);
}

TEST_CASE("LineReader: delivers empty lines as empty", "[protocol]") {
  LineReader reader;
  std::vector<std::string> lines;
  feed(reader, "\n\nabc\n\n", lines);
  REQUIRE(lines.size() == 4);
  CHECK(lines[0].empty());
  CHECK(lines[1].empty());
  CHECK(lines[2] == "abc");
  CHECK(lines[3].empty());
  CHECK(reader.dropped() == 0);
}

TEST_CASE("LineReader: feeding nothing changes nothing", "[protocol]") {
  LineReader reader;
  std::vector<std::string> lines;
  feed(reader, "ab", lines);
  feed(reader, "", lines);
  CHECK(lines.empty());
  CHECK(reader.pending() == 2);
}

TEST_CASE("LineReader: a line of exactly kMaxLineLength bytes is delivered", "[protocol]") {
  LineReader reader;
  std::vector<std::string> lines;
  const std::string line(kMaxLineLength, 'x');
  feed(reader, line + "\n", lines);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].size() == kMaxLineLength);
  CHECK(reader.dropped() == 0);
}

TEST_CASE("LineReader: a line one byte over kMaxLineLength is dropped and counted", "[protocol]") {
  LineReader reader;
  std::vector<std::string> lines;
  const std::string line(kMaxLineLength + 1, 'x');
  feed(reader, line + "\n", lines);
  CHECK(lines.empty());
  CHECK(reader.dropped() == 1);
  CHECK(reader.pending() == 0);
}

TEST_CASE("LineReader: the remainder of an overlong line is discarded up to the newline", "[protocol]") {
  LineReader reader;
  std::vector<std::string> lines;
  const std::string garbage(kMaxLineLength * 4, 'g');
  feed(reader, garbage, lines);
  CHECK(lines.empty());
  CHECK(reader.dropped() == 0);  // not counted until the line actually ends
  feed(reader, "still garbage", lines);
  feed(reader, "\n", lines);
  CHECK(lines.empty());
  CHECK(reader.dropped() == 1);
  CHECK(reader.pending() == 0);
}

TEST_CASE("LineReader: recovers cleanly on the line after an overflow", "[protocol]") {
  LineReader reader;
  std::vector<std::string> lines;
  const std::string garbage(kMaxLineLength + 10, 'g');
  feed(reader, garbage + "\n" + std::string(kOracleSpecLine) + "\n", lines);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == kOracleSpecLine);
  CHECK(reader.dropped() == 1);
}

TEST_CASE("LineReader: counts every overlong line", "[protocol]") {
  LineReader reader;
  std::vector<std::string> lines;
  const std::string garbage(kMaxLineLength + 1, 'g');
  feed(reader, garbage + "\n" + garbage + "\n" + "ok\n" + garbage + "\n", lines);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == "ok");
  CHECK(reader.dropped() == 3);
}

TEST_CASE("LineReader: an overlong line split across chunks is still a single drop", "[protocol]") {
  LineReader reader;
  std::vector<std::string> lines;
  const std::string half(kMaxLineLength, 'g');
  feed(reader, half, lines);
  feed(reader, half, lines);
  feed(reader, half + "\n", lines);
  CHECK(lines.empty());
  CHECK(reader.dropped() == 1);
}

TEST_CASE("LineReader: garbage before the first newline costs only that line", "[protocol]") {
  LineReader reader;
  std::vector<std::string> lines;
  const std::string garbage("\x00\xff\x7f junk", 8);  // explicit length: the NUL is part of the garbage
  feed(reader, garbage + "\n" + std::string(kOracleFaultLine) + "\n", lines);
  REQUIRE(lines.size() == 2);
  CHECK(parse_frame(lines[0]).error == ParseError::MissingCrc);
  CHECK(parse_frame(lines[1]).error == ParseError::None);
}

TEST_CASE("LineReader: a stream of frames with one corrupted parses the rest", "[protocol]") {
  LineReader reader;
  std::vector<std::string> lines;
  const std::string good = line_of(Scalars{.t = "100"});
  const std::string bad = seal_with(object_of(Scalars{.t = "200"}), "dead");
  const std::string also_good = line_of(Scalars{.t = "300"});
  feed(reader, good + "\n" + bad + "\r\n" + also_good + "\n", lines);
  REQUIRE(lines.size() == 3);

  std::size_t frames = 0;
  std::size_t crc_errors = 0;
  for (const auto& line : lines) {
    const ParseResult r = parse_frame(line);
    if (r.frame) {
      ++frames;
    } else if (r.error == ParseError::CrcMismatch) {
      ++crc_errors;
    }
  }
  CHECK(frames == 2);
  CHECK(crc_errors == 1);
  CHECK(parse_frame(lines[0]).frame->t_ms == 100U);
  CHECK(parse_frame(lines[2]).frame->t_ms == 300U);
}

}  // namespace
