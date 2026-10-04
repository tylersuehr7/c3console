#include "telemetry/csv_codec.hpp"
#include "telemetry/frame.hpp"
#include "vehicle/vehicle_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using c3::telemetry::Channel;
using c3::telemetry::CsvError;
using c3::telemetry::Fault;
using c3::telemetry::FaultKind;
using c3::telemetry::format_csv_row;
using c3::telemetry::Frame;
using c3::telemetry::is_csv_header;
using c3::telemetry::kCsvFaultDelimiter;
using c3::telemetry::kCsvHeader;
using c3::telemetry::kMaxCsvRowLength;
using c3::telemetry::parse_csv_row;
using Catch::Matchers::WithinAbs;

CATCH_REGISTER_ENUM(CsvError, CsvError::None, CsvError::FieldCount, CsvError::BadNumber, CsvError::OutOfRange,
                    CsvError::UnknownFault, CsvError::TooManyFaults)

namespace {

/// The spec §9.3 example values, written at the spec's precision.
constexpr std::string_view kCanonicalRow = "184523,12843,8211,19004,21877,0.6200,41.20,196.40,14.100,";

[[nodiscard]] Frame canonical_frame() noexcept {
  Frame f;
  f.t_ms = 184523;
  f.raw = {12843, 8211, 19004, 21877};
  f.fuel_fraction = 0.62F;
  f.oil_psi = 41.2F;
  f.coolant_f = 196.4F;
  f.voltage_v = 14.1F;
  return f;
}

[[nodiscard]] std::string format(const Frame& frame) {
  std::array<char, kMaxCsvRowLength> row{};
  const std::size_t n = format_csv_row(frame, row);
  REQUIRE(n > 0);
  return std::string(row.data(), n);
}

[[nodiscard]] Frame expect_row(std::string_view row) {
  INFO("row: " << row);
  Frame out;
  REQUIRE(parse_csv_row(row, out) == CsvError::None);
  return out;
}

/// Asserts the error and that a failed parse leaves `out` untouched.
void expect_error(std::string_view row, CsvError expected) {
  INFO("row: " << row);
  Frame out;
  out.t_ms = 777;
  out.oil_psi = 7.0F;
  CHECK(parse_csv_row(row, out) == expected);
  CHECK(out.t_ms == 777U);
  CHECK(out.oil_psi == 7.0F);
}

/// Owning split so callers may pass a temporary row.
[[nodiscard]] std::vector<std::string> split(std::string_view s, char sep) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || s[i] == sep) {
      parts.emplace_back(s.substr(start, i - start));
      start = i + 1;
    }
  }
  return parts;
}

// ---------------------------------------------------------------------------
// Constants and header
// ---------------------------------------------------------------------------

TEST_CASE("kCsvHeader: matches the spec column list exactly", "[csv_codec]") {
  STATIC_CHECK(kCsvHeader == "timestamp_ms,fuel_raw,oil_raw,clt_raw,volt_raw,fuel,oil,clt,volt,faults");
  const auto columns = split(kCsvHeader, ',');
  REQUIRE(columns.size() == 10);
  CHECK(columns[0] == "timestamp_ms");
  CHECK(columns[4] == "volt_raw");
  CHECK(columns[5] == "fuel");
  CHECK(columns[9] == "faults");
}

TEST_CASE("kCsvFaultDelimiter: is the pipe the spec chose", "[csv_codec]") {
  STATIC_CHECK(kCsvFaultDelimiter == '|');
}

TEST_CASE("is_csv_header: exact header with or without a carriage return", "[csv_codec]") {
  CHECK(is_csv_header(kCsvHeader));
  CHECK(is_csv_header(std::string(kCsvHeader) + "\r"));
}

TEST_CASE("is_csv_header: anything else is not the header", "[csv_codec]") {
  CHECK_FALSE(is_csv_header(""));
  CHECK_FALSE(is_csv_header("\r"));
  CHECK_FALSE(is_csv_header(std::string(kCsvHeader) + "\n"));
  CHECK_FALSE(is_csv_header(std::string(kCsvHeader) + " "));
  CHECK_FALSE(is_csv_header(" " + std::string(kCsvHeader)));
  CHECK_FALSE(is_csv_header(std::string(kCsvHeader) + ",relays"));
  CHECK_FALSE(is_csv_header(std::string(kCsvHeader).substr(1)));
  CHECK_FALSE(is_csv_header("TIMESTAMP_MS,fuel_raw,oil_raw,clt_raw,volt_raw,fuel,oil,clt,volt,faults"));
  CHECK_FALSE(is_csv_header(kCanonicalRow));
}

TEST_CASE("to_string: every CsvError has a distinct name", "[csv_codec]") {
  using c3::telemetry::to_string;
  CHECK(to_string(CsvError::None) == "ok");
  CHECK(to_string(CsvError::FieldCount) == "wrong field count");
  CHECK(to_string(CsvError::BadNumber) == "bad number");
  CHECK(to_string(CsvError::OutOfRange) == "value out of range");
  CHECK(to_string(CsvError::UnknownFault) == "unknown fault code");
  CHECK(to_string(CsvError::TooManyFaults) == "too many faults");
}

// ---------------------------------------------------------------------------
// format_csv_row
// ---------------------------------------------------------------------------

TEST_CASE("format_csv_row: writes the spec example at the spec precision", "[csv_codec]") {
  CHECK(format(canonical_frame()) == kCanonicalRow);
}

TEST_CASE("format_csv_row: a default frame writes zeros and an empty fault list", "[csv_codec]") {
  CHECK(format(Frame{}) == "0,0,0,0,0,0.0000,0.00,0.00,0.000,");
}

TEST_CASE("format_csv_row: exactly ten comma-separated fields, the last empty when clear", "[csv_codec]") {
  const auto fields = split(format(canonical_frame()), ',');
  REQUIRE(fields.size() == 10);
  CHECK(fields[0] == "184523");
  CHECK(fields[1] == "12843");
  CHECK(fields[2] == "8211");
  CHECK(fields[3] == "19004");
  CHECK(fields[4] == "21877");
  CHECK(fields[5] == "0.6200");
  CHECK(fields[6] == "41.20");
  CHECK(fields[7] == "196.40");
  CHECK(fields[8] == "14.100");
  CHECK(fields[9].empty());
}

TEST_CASE("format_csv_row: fuel is written with four decimals and rounds half away", "[csv_codec]") {
  Frame f;
  f.fuel_fraction = 0.123456F;
  CHECK(split(format(f), ',')[5] == "0.1235");
  f.fuel_fraction = 1.0F;
  CHECK(split(format(f), ',')[5] == "1.0000");
  f.fuel_fraction = 0.00004F;
  CHECK(split(format(f), ',')[5] == "0.0000");
}

TEST_CASE("format_csv_row: oil and coolant are written with two decimals", "[csv_codec]") {
  Frame f;
  f.oil_psi = 41.256F;
  f.coolant_f = -12.5F;
  const auto fields = split(format(f), ',');
  CHECK(fields[6] == "41.26");
  CHECK(fields[7] == "-12.50");
}

TEST_CASE("format_csv_row: volts are written with three decimals", "[csv_codec]") {
  Frame f;
  f.voltage_v = 14.1234F;
  CHECK(split(format(f), ',')[8] == "14.123");
  f.voltage_v = 12.0F;
  CHECK(split(format(f), ',')[8] == "12.000");
}

TEST_CASE("format_csv_row: timestamp spans the full uint32 range", "[csv_codec]") {
  Frame f;
  f.t_ms = std::numeric_limits<std::uint32_t>::max();
  CHECK(split(format(f), ',')[0] == "4294967295");
}

TEST_CASE("format_csv_row: raw counts may be negative and span int32", "[csv_codec]") {
  Frame f;
  f.raw = {-32768, std::numeric_limits<std::int32_t>::max(), std::numeric_limits<std::int32_t>::min(), 0};
  const auto fields = split(format(f), ',');
  CHECK(fields[1] == "-32768");
  CHECK(fields[2] == "2147483647");
  CHECK(fields[3] == "-2147483648");
  CHECK(fields[4] == "0");
}

TEST_CASE("format_csv_row: a single fault is written as its wire code", "[csv_codec]") {
  Frame f = canonical_frame();
  REQUIRE(f.faults.push_back(Fault{.kind = FaultKind::OilLow}));
  CHECK(format(f) == std::string(kCanonicalRow) + "OIL_LOW");
}

TEST_CASE("format_csv_row: several faults are joined with the delimiter in frame order", "[csv_codec]") {
  Frame f = canonical_frame();
  REQUIRE(f.faults.push_back(Fault{.kind = FaultKind::VoltLow}));
  REQUIRE(f.faults.push_back(Fault{.kind = FaultKind::OilLow}));
  REQUIRE(f.faults.push_back(Fault{.kind = FaultKind::SenderOpen, .channel = Channel::Coolant}));
  CHECK(format(f) == std::string(kCanonicalRow) + "VOLT_LOW|OIL_LOW|SENDER_OPEN_CLT");
}

TEST_CASE("format_csv_row: sender faults carry the spec channel spelling", "[csv_codec]") {
  Frame f;
  REQUIRE(f.faults.push_back(Fault{.kind = FaultKind::SenderShort, .channel = Channel::Fuel}));
  REQUIRE(f.faults.push_back(Fault{.kind = FaultKind::SenderShort, .channel = Channel::Oil}));
  REQUIRE(f.faults.push_back(Fault{.kind = FaultKind::SenderShort, .channel = Channel::Coolant}));
  REQUIRE(f.faults.push_back(Fault{.kind = FaultKind::SenderShort, .channel = Channel::Volt}));
  CHECK(split(format(f), ',')[9] == "SENDER_SHORT_FUEL|SENDER_SHORT_OIL|SENDER_SHORT_CLT|SENDER_SHORT_VOLT");
}

TEST_CASE("format_csv_row: no field is ever quoted", "[csv_codec]") {
  Frame f = canonical_frame();
  REQUIRE(f.faults.push_back(Fault{.kind = FaultKind::OilLow}));
  REQUIRE(f.faults.push_back(Fault{.kind = FaultKind::CltHigh}));
  const std::string row = format(f);
  CHECK(row.find('"') == std::string::npos);
  CHECK(row.find('\'') == std::string::npos);
  CHECK(row.find('\n') == std::string::npos);
  CHECK(row.find('\r') == std::string::npos);
}

TEST_CASE("format_csv_row: relays and reverse are not recorded", "[csv_codec]") {
  Frame plain = canonical_frame();
  Frame busy = canonical_frame();
  busy.reverse = true;
  busy.relays = c3::vehicle::RelayState{
      .blower = c3::vehicle::HvacBlower::L4,
      .mode = c3::vehicle::HvacMode::Defrost,
      .ac_enabled = true,
      .headlights_on = true,
      .lamp_doors_open = true,
      .highbeams_on = true,
  };
  CHECK(format(plain) == format(busy));
}

TEST_CASE("format_csv_row: the longest in-range row fits kMaxCsvRowLength with room for a terminator", "[csv_codec]") {
  Frame f;
  f.t_ms = std::numeric_limits<std::uint32_t>::max();
  f.raw.fill(std::numeric_limits<std::int32_t>::min());
  f.fuel_fraction = 1.0F;
  f.oil_psi = 200.0F;
  f.coolant_f = -40.0F;
  f.voltage_v = 40.0F;
  for (const auto channel : {Channel::Fuel, Channel::Oil, Channel::Coolant, Channel::Volt}) {
    REQUIRE(f.faults.push_back(Fault{.kind = FaultKind::SenderOpen, .channel = channel}));
    REQUIRE(f.faults.push_back(Fault{.kind = FaultKind::SenderShort, .channel = channel}));
  }
  REQUIRE(f.faults.full());

  std::array<char, kMaxCsvRowLength> row{};
  const std::size_t n = format_csv_row(f, row);
  CHECK(n > 0);
  CHECK(n < kMaxCsvRowLength);  // the recorder appends '\n' at index n
}

TEST_CASE("format_csv_row: needs one byte more than the row for the terminator slot", "[csv_codec]") {
  const Frame f = canonical_frame();
  const std::string expected = format(f);

  std::vector<char> exact(expected.size());
  CHECK(format_csv_row(f, exact) == 0);

  std::vector<char> plus_one(expected.size() + 1);
  const std::size_t n = format_csv_row(f, plus_one);
  CHECK(n == expected.size());
  CHECK(std::string_view(plus_one.data(), n) == expected);
}

TEST_CASE("format_csv_row: returns 0 for a buffer that cannot hold the numeric fields", "[csv_codec]") {
  std::array<char, 16> small{};
  CHECK(format_csv_row(canonical_frame(), small) == 0);
  CHECK(format_csv_row(canonical_frame(), std::span<char>{}) == 0);
}

TEST_CASE("format_csv_row: returns 0 when the fault list does not fit", "[csv_codec]") {
  Frame f = canonical_frame();
  REQUIRE(f.faults.push_back(Fault{.kind = FaultKind::OilLow}));
  REQUIRE(f.faults.push_back(Fault{.kind = FaultKind::CltHigh}));
  const std::string full = format(f);

  // Room for the numbers and the first fault, but not the delimiter and second code.
  std::vector<char> partial(kCanonicalRow.size() + std::string_view("OIL_LOW").size() + 3);
  CHECK(format_csv_row(f, partial) == 0);

  std::vector<char> enough(full.size() + 1);
  CHECK(format_csv_row(f, enough) == full.size());
}

// ---------------------------------------------------------------------------
// parse_csv_row: accepted rows
// ---------------------------------------------------------------------------

TEST_CASE("parse_csv_row: the spec example row populates every recorded member", "[csv_codec]") {
  const Frame f = expect_row(kCanonicalRow);
  CHECK(f.t_ms == 184523U);
  CHECK(f.raw[static_cast<std::size_t>(Channel::Fuel)] == 12843);
  CHECK(f.raw[static_cast<std::size_t>(Channel::Oil)] == 8211);
  CHECK(f.raw[static_cast<std::size_t>(Channel::Coolant)] == 19004);
  CHECK(f.raw[static_cast<std::size_t>(Channel::Volt)] == 21877);
  CHECK_THAT(f.fuel_fraction, WithinAbs(0.62, 1e-6));
  CHECK_THAT(f.oil_psi, WithinAbs(41.2, 1e-5));
  CHECK_THAT(f.coolant_f, WithinAbs(196.4, 1e-4));
  CHECK_THAT(f.voltage_v, WithinAbs(14.1, 1e-5));
  CHECK(f.faults.empty());
}

TEST_CASE("parse_csv_row: members that are not recorded come back at their defaults", "[csv_codec]") {
  Frame out;
  out.reverse = true;
  out.relays = c3::vehicle::RelayState{.blower = c3::vehicle::HvacBlower::L2};
  REQUIRE(out.faults.push_back(Fault{.kind = FaultKind::OilLow}));

  REQUIRE(parse_csv_row(kCanonicalRow, out) == CsvError::None);
  CHECK_FALSE(out.reverse);
  CHECK_FALSE(out.relays.has_value());
  CHECK(out.faults.empty());
}

TEST_CASE("parse_csv_row: a trailing carriage return is tolerated", "[csv_codec]") {
  const Frame f = expect_row(std::string(kCanonicalRow) + "\r");
  CHECK(f.t_ms == 184523U);
  CHECK(f.faults.empty());

  const Frame with_fault = expect_row(std::string(kCanonicalRow) + "OIL_LOW\r");
  REQUIRE(with_fault.faults.size() == 1);
  CHECK(with_fault.faults[0].kind == FaultKind::OilLow);
}

TEST_CASE("parse_csv_row: number tokens may use any precision or notation the parser accepts", "[csv_codec]") {
  const Frame f = expect_row("1,0,0,0,0,.5,4e1,1.8E2,+12,");
  CHECK(f.fuel_fraction == 0.5F);
  CHECK(f.oil_psi == 40.0F);
  CHECK(f.coolant_f == 180.0F);
  CHECK(f.voltage_v == 12.0F);
}

TEST_CASE("parse_csv_row: timestamp spans the full uint32 range", "[csv_codec]") {
  CHECK(expect_row("0,0,0,0,0,0,0,0,0,").t_ms == 0U);
  CHECK(expect_row("4294967295,0,0,0,0,0,0,0,0,").t_ms == 4294967295U);
}

TEST_CASE("parse_csv_row: raw counts may be negative and span int32", "[csv_codec]") {
  const Frame f = expect_row("1,-32768,2147483647,-2147483648,0,0,0,0,0,");
  CHECK(f.raw[0] == -32768);
  CHECK(f.raw[1] == 2147483647);
  CHECK(f.raw[2] == -2147483647 - 1);
  CHECK(f.raw[3] == 0);
}

TEST_CASE("parse_csv_row: every simple fault code is recognised", "[csv_codec]") {
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
    const Frame f = expect_row(std::string(kCanonicalRow) + std::string(c.code));
    REQUIRE(f.faults.size() == 1);
    CHECK(f.faults[0].kind == c.kind);
  }
}

TEST_CASE("parse_csv_row: sender faults carry their channel", "[csv_codec]") {
  const Frame f = expect_row(std::string(kCanonicalRow) + "SENDER_OPEN_FUEL|SENDER_SHORT_CLT|SENDER_OPEN_VOLT");
  REQUIRE(f.faults.size() == 3);
  CHECK(f.faults[0].kind == FaultKind::SenderOpen);
  CHECK(f.faults[0].channel == Channel::Fuel);
  CHECK(f.faults[1].kind == FaultKind::SenderShort);
  CHECK(f.faults[1].channel == Channel::Coolant);
  CHECK(f.faults[2].kind == FaultKind::SenderOpen);
  CHECK(f.faults[2].channel == Channel::Volt);
}

TEST_CASE("parse_csv_row: faults keep file order", "[csv_codec]") {
  const Frame f = expect_row(std::string(kCanonicalRow) + "VOLT_HIGH|OIL_LOW|FUEL_LOW");
  REQUIRE(f.faults.size() == 3);
  CHECK(f.faults[0].kind == FaultKind::VoltHigh);
  CHECK(f.faults[1].kind == FaultKind::OilLow);
  CHECK(f.faults[2].kind == FaultKind::FuelLow);
}

TEST_CASE("parse_csv_row: eight faults fill the frame", "[csv_codec]") {
  const Frame f =
      expect_row(std::string(kCanonicalRow) +
                 "OIL_LOW|CLT_HIGH|FUEL_LOW|VOLT_LOW|VOLT_HIGH|SENDER_OPEN_FUEL|SENDER_OPEN_OIL|SENDER_SHORT_VOLT");
  REQUIRE(f.faults.size() == 8);
  CHECK(f.faults.full());
  CHECK(f.faults[7].kind == FaultKind::SenderShort);
  CHECK(f.faults[7].channel == Channel::Volt);
}

// ---------------------------------------------------------------------------
// parse_csv_row: field count
// ---------------------------------------------------------------------------

TEST_CASE("parse_csv_row: too few fields is FieldCount", "[csv_codec]") {
  expect_error("", CsvError::FieldCount);
  expect_error("\r", CsvError::FieldCount);
  expect_error("184523", CsvError::FieldCount);
  expect_error("184523,12843,8211,19004,21877,0.6200,41.20,196.40,14.100", CsvError::FieldCount);  // 9: no faults
  expect_error(",,,,,,,,", CsvError::FieldCount);                                                  // 9 empties
}

TEST_CASE("parse_csv_row: too many fields is FieldCount", "[csv_codec]") {
  expect_error(std::string(kCanonicalRow) + ",", CsvError::FieldCount);
  expect_error(std::string(kCanonicalRow) + "OIL_LOW,CLT_HIGH", CsvError::FieldCount);  // comma-joined faults
  expect_error(std::string(kCanonicalRow) + "true,false", CsvError::FieldCount);        // relays sneaking in
}

TEST_CASE("parse_csv_row: other separators are not recognised", "[csv_codec]") {
  expect_error("184523;12843;8211;19004;21877;0.6200;41.20;196.40;14.100;", CsvError::FieldCount);
  expect_error("184523\t12843\t8211\t19004\t21877\t0.6200\t41.20\t196.40\t14.100\t", CsvError::FieldCount);
}

TEST_CASE("parse_csv_row: the header row is not a data row", "[csv_codec]") {
  expect_error(kCsvHeader, CsvError::BadNumber);
}

// ---------------------------------------------------------------------------
// parse_csv_row: bad numbers
// ---------------------------------------------------------------------------

TEST_CASE("parse_csv_row: timestamp must be a uint32", "[csv_codec]") {
  expect_error("-1,0,0,0,0,0,0,0,0,", CsvError::BadNumber);
  expect_error("4294967296,0,0,0,0,0,0,0,0,", CsvError::BadNumber);
  expect_error("184523.0,0,0,0,0,0,0,0,0,", CsvError::BadNumber);
  expect_error("1e3,0,0,0,0,0,0,0,0,", CsvError::BadNumber);
  expect_error(",0,0,0,0,0,0,0,0,", CsvError::BadNumber);
  expect_error("t,0,0,0,0,0,0,0,0,", CsvError::BadNumber);
}

TEST_CASE("parse_csv_row: raw counts must be int32", "[csv_codec]") {
  expect_error("1,12843.5,0,0,0,0,0,0,0,", CsvError::BadNumber);
  expect_error("1,0,2147483648,0,0,0,0,0,0,", CsvError::BadNumber);
  expect_error("1,0,0,-2147483649,0,0,0,0,0,", CsvError::BadNumber);
  expect_error("1,0,0,0,,0,0,0,0,", CsvError::BadNumber);
  expect_error("1,0,0,0,0x10,0,0,0,0,", CsvError::BadNumber);
}

TEST_CASE("parse_csv_row: measurements must be finite numbers", "[csv_codec]") {
  expect_error("1,0,0,0,0,abc,0,0,0,", CsvError::BadNumber);
  expect_error("1,0,0,0,0,,0,0,0,", CsvError::BadNumber);
  expect_error("1,0,0,0,0,0,nan,0,0,", CsvError::BadNumber);
  expect_error("1,0,0,0,0,0,0,inf,0,", CsvError::BadNumber);
  expect_error("1,0,0,0,0,0,0,0,1e999,", CsvError::BadNumber);
  expect_error("1,0,0,0,0,0,0,0,0.5.5,", CsvError::BadNumber);
}

TEST_CASE("parse_csv_row: fields are not trimmed", "[csv_codec]") {
  expect_error(" 184523,12843,8211,19004,21877,0.6200,41.20,196.40,14.100,", CsvError::BadNumber);
  expect_error("184523 ,12843,8211,19004,21877,0.6200,41.20,196.40,14.100,", CsvError::BadNumber);
  expect_error("184523, 12843,8211,19004,21877,0.6200,41.20,196.40,14.100,", CsvError::BadNumber);
  expect_error("184523,12843,8211,19004,21877, 0.6200,41.20,196.40,14.100,", CsvError::BadNumber);
  expect_error("184523,12843,8211,19004,21877,0.6200,41.20,196.40,14.100 ,", CsvError::BadNumber);
}

TEST_CASE("parse_csv_row: quoted fields are rejected", "[csv_codec]") {
  expect_error("\"184523\",12843,8211,19004,21877,0.6200,41.20,196.40,14.100,", CsvError::BadNumber);
  expect_error("184523,12843,8211,19004,21877,\"0.6200\",41.20,196.40,14.100,", CsvError::BadNumber);
  expect_error(std::string(kCanonicalRow) + "\"OIL_LOW\"", CsvError::UnknownFault);
}

TEST_CASE("parse_csv_row: a trailing newline is not stripped", "[csv_codec]") {
  expect_error(std::string(kCanonicalRow) + "\n", CsvError::UnknownFault);
  expect_error(std::string(kCanonicalRow) + "OIL_LOW\n", CsvError::UnknownFault);
  expect_error(std::string(kCanonicalRow) + "\r\n", CsvError::UnknownFault);
}

// ---------------------------------------------------------------------------
// parse_csv_row: plausibility limits
// ---------------------------------------------------------------------------

TEST_CASE("parse_csv_row: fuel is accepted on 0 to 1 inclusive and rejected outside", "[csv_codec]") {
  CHECK(expect_row("1,0,0,0,0,0,0,0,0,").fuel_fraction == 0.0F);
  CHECK(expect_row("1,0,0,0,0,1,0,0,0,").fuel_fraction == 1.0F);
  expect_error("1,0,0,0,0,-0.0001,0,0,0,", CsvError::OutOfRange);
  expect_error("1,0,0,0,0,1.0001,0,0,0,", CsvError::OutOfRange);
  expect_error("1,0,0,0,0,62,0,0,0,", CsvError::OutOfRange);
}

TEST_CASE("parse_csv_row: oil is accepted on 0 to 200 inclusive and rejected outside", "[csv_codec]") {
  CHECK(expect_row("1,0,0,0,0,0,0,0,0,").oil_psi == 0.0F);
  CHECK(expect_row("1,0,0,0,0,0,200,0,0,").oil_psi == 200.0F);
  expect_error("1,0,0,0,0,0,-0.01,0,0,", CsvError::OutOfRange);
  expect_error("1,0,0,0,0,0,200.01,0,0,", CsvError::OutOfRange);
}

TEST_CASE("parse_csv_row: coolant is accepted on -40 to 400 inclusive and rejected outside", "[csv_codec]") {
  CHECK(expect_row("1,0,0,0,0,0,0,-40,0,").coolant_f == -40.0F);
  CHECK(expect_row("1,0,0,0,0,0,0,400,0,").coolant_f == 400.0F);
  expect_error("1,0,0,0,0,0,0,-40.5,0,", CsvError::OutOfRange);
  expect_error("1,0,0,0,0,0,0,400.5,0,", CsvError::OutOfRange);
}

TEST_CASE("parse_csv_row: volts are accepted on 0 to 40 inclusive and rejected outside", "[csv_codec]") {
  CHECK(expect_row("1,0,0,0,0,0,0,0,0,").voltage_v == 0.0F);
  CHECK(expect_row("1,0,0,0,0,0,0,0,40,").voltage_v == 40.0F);
  expect_error("1,0,0,0,0,0,0,0,-0.1,", CsvError::OutOfRange);
  expect_error("1,0,0,0,0,0,0,0,40.1,", CsvError::OutOfRange);
}

TEST_CASE("parse_csv_row: alarming but plausible values are accepted", "[csv_codec]") {
  const Frame f = expect_row("1,0,0,0,0,0.02,3,260,9.5,OIL_LOW|CLT_HIGH|FUEL_LOW|VOLT_LOW");
  CHECK(f.oil_psi == 3.0F);
  CHECK(f.coolant_f == 260.0F);
  CHECK(f.faults.size() == 4);
}

// ---------------------------------------------------------------------------
// parse_csv_row: faults
// ---------------------------------------------------------------------------

TEST_CASE("parse_csv_row: unknown or misspelt fault codes are UnknownFault", "[csv_codec]") {
  expect_error(std::string(kCanonicalRow) + "BRAKE_LOW", CsvError::UnknownFault);
  expect_error(std::string(kCanonicalRow) + "oil_low", CsvError::UnknownFault);
  expect_error(std::string(kCanonicalRow) + "OIL_LOW ", CsvError::UnknownFault);
  expect_error(std::string(kCanonicalRow) + "SENDER_OPEN", CsvError::UnknownFault);
  expect_error(std::string(kCanonicalRow) + "SENDER_OPEN_COOLANT", CsvError::UnknownFault);
  expect_error(std::string(kCanonicalRow) + "OIL_LOW|BRAKE_LOW", CsvError::UnknownFault);
}

TEST_CASE("parse_csv_row: empty entries in the fault list are UnknownFault", "[csv_codec]") {
  expect_error(std::string(kCanonicalRow) + "|", CsvError::UnknownFault);
  expect_error(std::string(kCanonicalRow) + "OIL_LOW|", CsvError::UnknownFault);
  expect_error(std::string(kCanonicalRow) + "|OIL_LOW", CsvError::UnknownFault);
  expect_error(std::string(kCanonicalRow) + "OIL_LOW||CLT_HIGH", CsvError::UnknownFault);
  expect_error(std::string(kCanonicalRow) + "OIL_LOW| CLT_HIGH", CsvError::UnknownFault);
}

TEST_CASE("parse_csv_row: nine faults is TooManyFaults", "[csv_codec]") {
  expect_error(std::string(kCanonicalRow) +
                   "OIL_LOW|CLT_HIGH|FUEL_LOW|VOLT_LOW|VOLT_HIGH|SENDER_OPEN_FUEL|SENDER_OPEN_OIL|SENDER_SHORT_VOLT|"
                   "SENDER_SHORT_OIL",
               CsvError::TooManyFaults);
}

TEST_CASE("parse_csv_row: the first failing field in column order decides the error", "[csv_codec]") {
  expect_error("x,0,0,0,0,5,0,0,0,BRAKE", CsvError::BadNumber);     // t before fuel and faults
  expect_error("1,x,0,0,0,5,0,0,0,BRAKE", CsvError::BadNumber);     // raw before fuel
  expect_error("1,0,0,0,0,5,x,0,0,BRAKE", CsvError::OutOfRange);    // fuel range before oil format
  expect_error("1,0,0,0,0,0,0,0,99,BRAKE", CsvError::OutOfRange);   // volt before faults
  expect_error("1,0,0,0,0,0,0,0,0,BRAKE,x", CsvError::FieldCount);  // field count before everything
}

// ---------------------------------------------------------------------------
// Round trips
// ---------------------------------------------------------------------------

TEST_CASE("round trip: format then parse recovers exact integers and precision-bounded values", "[csv_codec]") {
  Frame in = canonical_frame();
  in.fuel_fraction = 0.123456F;
  in.oil_psi = 41.256F;
  in.coolant_f = 196.449F;
  in.voltage_v = 14.1234F;
  in.raw = {-5, 32767, -32768, 1};
  REQUIRE(in.faults.push_back(Fault{.kind = FaultKind::OilLow}));
  REQUIRE(in.faults.push_back(Fault{.kind = FaultKind::SenderShort, .channel = Channel::Coolant}));

  const Frame out = expect_row(format(in));
  CHECK(out.t_ms == in.t_ms);
  CHECK(out.raw == in.raw);
  CHECK_THAT(out.fuel_fraction, WithinAbs(in.fuel_fraction, 0.00005));
  CHECK_THAT(out.oil_psi, WithinAbs(in.oil_psi, 0.005));
  CHECK_THAT(out.coolant_f, WithinAbs(in.coolant_f, 0.005));
  CHECK_THAT(out.voltage_v, WithinAbs(in.voltage_v, 0.0005));
  REQUIRE(out.faults.size() == 2);
  CHECK(out.faults[0].kind == FaultKind::OilLow);
  CHECK(out.faults[1].kind == FaultKind::SenderShort);
  CHECK(out.faults[1].channel == Channel::Coolant);
}

TEST_CASE("round trip: a full frame of eight faults survives", "[csv_codec]") {
  Frame in = canonical_frame();
  for (const auto channel : {Channel::Fuel, Channel::Oil, Channel::Coolant, Channel::Volt}) {
    REQUIRE(in.faults.push_back(Fault{.kind = FaultKind::SenderOpen, .channel = channel}));
    REQUIRE(in.faults.push_back(Fault{.kind = FaultKind::SenderShort, .channel = channel}));
  }
  const Frame out = expect_row(format(in));
  REQUIRE(out.faults.size() == 8);
  for (std::size_t i = 0; i < 8; ++i) {
    CHECK(out.faults[i].kind == in.faults[i].kind);
    CHECK(out.faults[i].channel == in.faults[i].channel);
  }
}

TEST_CASE("round trip: parse then format reproduces a spec-precision row exactly", "[csv_codec]") {
  for (const std::string_view row : {
           kCanonicalRow,
           std::string_view("0,0,0,0,0,0.0000,0.00,0.00,0.000,"),
           std::string_view("4294967295,-2147483648,2147483647,-1,1,1.0000,200.00,-40.00,40.000,OIL_LOW"),
           std::string_view("7,1,2,3,4,0.5000,12.34,98.76,13.800,VOLT_LOW|SENDER_OPEN_CLT|CLT_HIGH"),
       }) {
    INFO("row: " << row);
    CHECK(format(expect_row(row)) == row);
  }
}

TEST_CASE("round trip: a second format of a parsed row is stable", "[csv_codec]") {
  Frame in = canonical_frame();
  in.fuel_fraction = 0.33333F;
  in.voltage_v = 13.8765F;
  const std::string first = format(in);
  const std::string second = format(expect_row(first));
  CHECK(first == second);
}

TEST_CASE("round trip: replay precision loss stays within the spec's written precision", "[csv_codec]") {
  for (const float fuel : {0.0F, 0.00005F, 0.12345F, 0.99995F, 1.0F}) {
    Frame in;
    in.fuel_fraction = fuel;
    in.oil_psi = fuel * 200.0F;
    in.coolant_f = -40.0F + fuel * 440.0F;
    in.voltage_v = fuel * 40.0F;
    // Half a unit in the last written place, plus float representation error at that scale.
    const Frame out = expect_row(format(in));
    CHECK_THAT(out.fuel_fraction, WithinAbs(in.fuel_fraction, 0.00005 + 1e-6));
    CHECK_THAT(out.oil_psi, WithinAbs(in.oil_psi, 0.005 + 1e-4));
    CHECK_THAT(out.coolant_f, WithinAbs(in.coolant_f, 0.005 + 1e-4));
    CHECK_THAT(out.voltage_v, WithinAbs(in.voltage_v, 0.0005 + 1e-5));
  }
}

}  // namespace
