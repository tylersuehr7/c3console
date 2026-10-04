#include "utils/parse_number.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

using c3::utils::parse_float;
using c3::utils::parse_int;
using Catch::Matchers::WithinRel;

namespace {

// ---------------------------------------------------------------------------
// parse_float: accepted tokens
// ---------------------------------------------------------------------------

TEST_CASE("parse_float: plain integers and decimals", "[parse_number]") {
  float v = -1.0F;
  REQUIRE(parse_float("0", v));
  CHECK(v == 0.0F);
  REQUIRE(parse_float("42", v));
  CHECK(v == 42.0F);
  REQUIRE(parse_float("0.62", v));
  CHECK_THAT(v, WithinRel(0.62F, 1e-6F));
  REQUIRE(parse_float("196.4", v));
  CHECK_THAT(v, WithinRel(196.4F, 1e-6F));
}

TEST_CASE("parse_float: negative values and signed zero", "[parse_number]") {
  float v = 1.0F;
  REQUIRE(parse_float("-40", v));
  CHECK(v == -40.0F);
  REQUIRE(parse_float("-0.25", v));
  CHECK(v == -0.25F);
  REQUIRE(parse_float("-0", v));
  CHECK(v == 0.0F);
  CHECK(std::signbit(v));
}

TEST_CASE("parse_float: explicit plus sign is accepted", "[parse_number]") {
  float v = 0.0F;
  REQUIRE(parse_float("+14.1", v));
  CHECK_THAT(v, WithinRel(14.1F, 1e-6F));
}

TEST_CASE("parse_float: leading or trailing decimal point without digits on the other side", "[parse_number]") {
  float v = 0.0F;
  REQUIRE(parse_float(".5", v));
  CHECK(v == 0.5F);
  REQUIRE(parse_float("5.", v));
  CHECK(v == 5.0F);
}

TEST_CASE("parse_float: scientific notation in both cases and signs", "[parse_number]") {
  float v = 0.0F;
  REQUIRE(parse_float("6.2e-1", v));
  CHECK_THAT(v, WithinRel(0.62F, 1e-6F));
  REQUIRE(parse_float("1E3", v));
  CHECK(v == 1000.0F);
  REQUIRE(parse_float("2.5e+2", v));
  CHECK(v == 250.0F);
  REQUIRE(parse_float("1e0", v));
  CHECK(v == 1.0F);
}

TEST_CASE("parse_float: recorder precision tokens round-trip", "[parse_number]") {
  // The CSV recorder writes fuel %.4f, oil %.2f, clt %.2f, volt %.3f.
  float v = 0.0F;
  REQUIRE(parse_float("0.6200", v));
  CHECK_THAT(v, WithinRel(0.62F, 1e-6F));
  REQUIRE(parse_float("41.20", v));
  CHECK_THAT(v, WithinRel(41.2F, 1e-6F));
  REQUIRE(parse_float("14.100", v));
  CHECK_THAT(v, WithinRel(14.1F, 1e-6F));
}

TEST_CASE("parse_float: longest accepted token is 31 characters", "[parse_number]") {
  float v = 0.0F;
  const std::string thirty_one = "0." + std::string(29, '1');
  REQUIRE(thirty_one.size() == 31);
  CHECK(parse_float(thirty_one, v));
  CHECK_THAT(v, WithinRel(0.1111111F, 1e-5F));
}

TEST_CASE("parse_float: values at the edge of float range are accepted", "[parse_number]") {
  float v = 0.0F;
  REQUIRE(parse_float("3.4028235e38", v));
  CHECK(std::isfinite(v));
  CHECK(v == std::numeric_limits<float>::max());
  REQUIRE(parse_float("-3.4028235e38", v));
  CHECK(v == std::numeric_limits<float>::lowest());
  REQUIRE(parse_float("1e-45", v));
  CHECK(std::isfinite(v));
}

// ---------------------------------------------------------------------------
// parse_float: rejected tokens
// ---------------------------------------------------------------------------

TEST_CASE("parse_float: rejects empty token", "[parse_number]") {
  float v = 7.0F;
  CHECK_FALSE(parse_float("", v));
  CHECK(v == 7.0F);
}

TEST_CASE("parse_float: rejects tokens of 32 characters or more", "[parse_number]") {
  float v = 7.0F;
  const std::string thirty_two = "0." + std::string(30, '1');
  REQUIRE(thirty_two.size() == 32);
  CHECK_FALSE(parse_float(thirty_two, v));
  CHECK_FALSE(parse_float(std::string(100, '1'), v));
  CHECK(v == 7.0F);
}

TEST_CASE("parse_float: rejects trailing characters", "[parse_number]") {
  float v = 7.0F;
  CHECK_FALSE(parse_float("1.5x", v));
  CHECK_FALSE(parse_float("1.5 ", v));
  CHECK_FALSE(parse_float("1.5,", v));
  CHECK_FALSE(parse_float("1.5\n", v));
  CHECK_FALSE(parse_float("1.5\r", v));
  CHECK_FALSE(parse_float("1.5e", v));
  CHECK_FALSE(parse_float("1.5e+", v));
  CHECK(v == 7.0F);
}

TEST_CASE("parse_float: rejects leading whitespace that strtod would otherwise skip", "[parse_number]") {
  float v = 7.0F;
  CHECK_FALSE(parse_float(" 1.5", v));
  CHECK_FALSE(parse_float("\t1.5", v));
  CHECK_FALSE(parse_float("\n1.5", v));
  CHECK_FALSE(parse_float("\r1.5", v));
  CHECK_FALSE(parse_float("  1", v));
  CHECK(v == 7.0F);
}

TEST_CASE("parse_float: rejects non-numeric text", "[parse_number]") {
  float v = 7.0F;
  CHECK_FALSE(parse_float("abc", v));
  CHECK_FALSE(parse_float("-", v));
  CHECK_FALSE(parse_float("+", v));
  CHECK_FALSE(parse_float(".", v));
  CHECK_FALSE(parse_float("e5", v));
  CHECK_FALSE(parse_float("0.5.5", v));
  CHECK_FALSE(parse_float("1,5", v));
  CHECK_FALSE(parse_float("true", v));
  CHECK_FALSE(parse_float("null", v));
  CHECK(v == 7.0F);
}

TEST_CASE("parse_float: rejects NaN spellings", "[parse_number]") {
  float v = 7.0F;
  CHECK_FALSE(parse_float("nan", v));
  CHECK_FALSE(parse_float("NaN", v));
  CHECK_FALSE(parse_float("-nan", v));
  CHECK_FALSE(parse_float("nan(123)", v));
  CHECK(v == 7.0F);
}

TEST_CASE("parse_float: rejects infinity spellings", "[parse_number]") {
  float v = 7.0F;
  CHECK_FALSE(parse_float("inf", v));
  CHECK_FALSE(parse_float("INF", v));
  CHECK_FALSE(parse_float("-inf", v));
  CHECK_FALSE(parse_float("infinity", v));
  CHECK_FALSE(parse_float("Infinity", v));
  CHECK(v == 7.0F);
}

TEST_CASE("parse_float: rejects magnitudes that overflow double", "[parse_number]") {
  float v = 7.0F;
  CHECK_FALSE(parse_float("1e999", v));
  CHECK_FALSE(parse_float("-1e999", v));
  CHECK(v == 7.0F);
}

TEST_CASE("parse_float: rejects magnitudes that fit double but overflow float", "[parse_number]") {
  float v = 7.0F;
  CHECK_FALSE(parse_float("1e39", v));
  CHECK_FALSE(parse_float("-1e39", v));
  CHECK_FALSE(parse_float("3.5e38", v));
  CHECK_FALSE(parse_float("3.4028236e38", v));  // first decimal above the round-to-inf boundary
  CHECK(v == 7.0F);
}

TEST_CASE("parse_float: out is untouched on every failure path", "[parse_number]") {
  float v = 12.5F;
  CHECK_FALSE(parse_float("", v));
  CHECK_FALSE(parse_float("x", v));
  CHECK_FALSE(parse_float("1x", v));
  CHECK_FALSE(parse_float("nan", v));
  CHECK_FALSE(parse_float("1e39", v));
  CHECK(v == 12.5F);
}

// ---------------------------------------------------------------------------
// parse_int: accepted tokens
// ---------------------------------------------------------------------------

TEST_CASE("parse_int: int32 positive, zero and negative", "[parse_number]") {
  std::int32_t v = -1;
  REQUIRE(parse_int("12843", v));
  CHECK(v == 12843);
  REQUIRE(parse_int("0", v));
  CHECK(v == 0);
  REQUIRE(parse_int("-8211", v));
  CHECK(v == -8211);
}

TEST_CASE("parse_int: int32 extremes", "[parse_number]") {
  std::int32_t v = 0;
  REQUIRE(parse_int("2147483647", v));
  CHECK(v == std::numeric_limits<std::int32_t>::max());
  REQUIRE(parse_int("-2147483648", v));
  CHECK(v == std::numeric_limits<std::int32_t>::min());
}

TEST_CASE("parse_int: uint32 extremes", "[parse_number]") {
  std::uint32_t v = 1;
  REQUIRE(parse_int("0", v));
  CHECK(v == 0U);
  REQUIRE(parse_int("4294967295", v));
  CHECK(v == std::numeric_limits<std::uint32_t>::max());
  REQUIRE(parse_int("184523", v));
  CHECK(v == 184523U);
}

TEST_CASE("parse_int: narrow and wide integer types", "[parse_number]") {
  std::uint8_t u8 = 0;
  REQUIRE(parse_int("255", u8));
  CHECK(u8 == 255);

  std::int8_t i8 = 0;
  REQUIRE(parse_int("-128", i8));
  CHECK(i8 == -128);

  std::uint16_t u16 = 0;
  REQUIRE(parse_int("65535", u16));
  CHECK(u16 == 65535);

  std::int64_t i64 = 0;
  REQUIRE(parse_int("-9223372036854775808", i64));
  CHECK(i64 == std::numeric_limits<std::int64_t>::min());

  std::uint64_t u64 = 0;
  REQUIRE(parse_int("18446744073709551615", u64));
  CHECK(u64 == std::numeric_limits<std::uint64_t>::max());
}

TEST_CASE("parse_int: leading zeros are accepted as decimal", "[parse_number]") {
  std::int32_t v = 0;
  REQUIRE(parse_int("007", v));
  CHECK(v == 7);
  REQUIRE(parse_int("-0", v));
  CHECK(v == 0);
}

// ---------------------------------------------------------------------------
// parse_int: rejected tokens
// ---------------------------------------------------------------------------

TEST_CASE("parse_int: rejects empty token", "[parse_number]") {
  std::int32_t v = 99;
  CHECK_FALSE(parse_int("", v));
  CHECK(v == 99);
}

TEST_CASE("parse_int: rejects an explicit plus sign", "[parse_number]") {
  std::int32_t v = 99;
  CHECK_FALSE(parse_int("+5", v));
  CHECK(v == 99);
}

TEST_CASE("parse_int: rejects a minus sign for unsigned targets", "[parse_number]") {
  std::uint32_t v = 99;
  CHECK_FALSE(parse_int("-1", v));
  CHECK_FALSE(parse_int("-0", v));
  CHECK(v == 99U);
}

TEST_CASE("parse_int: rejects overflow for the target type", "[parse_number]") {
  std::int32_t i32 = 99;
  CHECK_FALSE(parse_int("2147483648", i32));
  CHECK_FALSE(parse_int("-2147483649", i32));
  CHECK(i32 == 99);

  std::uint32_t u32 = 99;
  CHECK_FALSE(parse_int("4294967296", u32));
  CHECK(u32 == 99U);

  std::uint8_t u8 = 9;
  CHECK_FALSE(parse_int("256", u8));
  CHECK(u8 == 9);

  std::int8_t i8 = 9;
  CHECK_FALSE(parse_int("128", i8));
  CHECK_FALSE(parse_int("-129", i8));
  CHECK(i8 == 9);
}

TEST_CASE("parse_int: rejects whitespace anywhere", "[parse_number]") {
  std::int32_t v = 99;
  CHECK_FALSE(parse_int(" 5", v));
  CHECK_FALSE(parse_int("5 ", v));
  CHECK_FALSE(parse_int("5\n", v));
  CHECK_FALSE(parse_int("5\r", v));
  CHECK_FALSE(parse_int("1 2", v));
  CHECK(v == 99);
}

TEST_CASE("parse_int: rejects trailing characters", "[parse_number]") {
  std::int32_t v = 99;
  CHECK_FALSE(parse_int("12x", v));
  CHECK_FALSE(parse_int("12,", v));
  CHECK_FALSE(parse_int("12}", v));
  CHECK(v == 99);
}

TEST_CASE("parse_int: rejects decimals and exponents", "[parse_number]") {
  std::int32_t v = 99;
  CHECK_FALSE(parse_int("5.0", v));
  CHECK_FALSE(parse_int("5.", v));
  CHECK_FALSE(parse_int(".5", v));
  CHECK_FALSE(parse_int("1e3", v));
  CHECK(v == 99);
}

TEST_CASE("parse_int: rejects non-decimal notations and text", "[parse_number]") {
  std::int32_t v = 99;
  CHECK_FALSE(parse_int("0x10", v));
  CHECK_FALSE(parse_int("abc", v));
  CHECK_FALSE(parse_int("-", v));
  CHECK_FALSE(parse_int("true", v));
  CHECK_FALSE(parse_int("null", v));
  CHECK(v == 99);
}

TEST_CASE("parse_int: out is untouched when the token has trailing garbage", "[parse_number]") {
  // from_chars stores a partial result before reporting where it stopped; parse_int must not
  // leak that partial value to the caller.
  std::int32_t v = 99;
  CHECK_FALSE(parse_int("12x", v));
  CHECK(v == 99);
  std::uint32_t u = 7;
  CHECK_FALSE(parse_int("184523.0", u));
  CHECK(u == 7U);
}

}  // namespace
