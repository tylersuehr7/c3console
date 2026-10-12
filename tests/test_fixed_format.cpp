#include "utils/fixed_format.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>

using c3::utils::format_fixed;
using c3::utils::format_into;

namespace {

TEST_CASE("format_fixed: decimals and rounding", "[fixed_format]") {
  std::array<char, 16> buf{};
  CHECK(format_fixed(buf, 41.26, 1) == "41.3");
  CHECK(format_fixed(buf, 41.26, 0) == "41");
  CHECK(format_fixed(buf, -0.04, 1) == "-0.0");
  CHECK(format_fixed(buf, 14.0, 1) == "14.0");
}

// GCC proves the truncation at compile time and warns; the truncation is what this test
// checks, so the warning is silenced for this case only.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif
TEST_CASE("format_into: truncates safely and stays terminated", "[fixed_format]") {
  std::array<char, 6> buf{};
  const auto v = format_into(buf, "%s", "0123456789");
  CHECK(v == "01234");
  CHECK(buf[5] == '\0');
  CHECK(v.size() == 5);
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

TEST_CASE("format_into: composite", "[fixed_format]") {
  std::array<char, 8> buf{};
  CHECK(format_into(buf, "%d:%02d", 4, 7) == "4:07");
}

TEST_CASE("format_fixed: negative values round away from zero at the chosen precision", "[fixed_format]") {
  std::array<char, 16> buf{};
  CHECK(format_fixed(buf, -2.46, 1) == "-2.5");
  CHECK(format_fixed(buf, -0.5, 0) == "-0");
}

TEST_CASE("format_fixed: more than one decimal", "[fixed_format]") {
  std::array<char, 16> buf{};
  CHECK(format_fixed(buf, 3.14159, 2) == "3.14");
  CHECK(format_fixed(buf, 3.14159, 4) == "3.1416");
}

TEST_CASE("format_fixed: zero decimals rounds to a whole number", "[fixed_format]") {
  std::array<char, 16> buf{};
  CHECK(format_fixed(buf, 1234.56, 0) == "1235");
  CHECK(format_fixed(buf, 0.0, 0) == "0");
}

TEST_CASE("format_fixed: truncates safely when the number does not fit", "[fixed_format]") {
  std::array<char, 4> buf{};
  // Opaque inputs: GCC would otherwise prove the truncation at compile time and warn from
  // inside the header, where no pragma in this file can reach.
  const volatile double value = 12345.678;
  const volatile int decimals = 2;
  const auto v = format_fixed(buf, value, decimals);
  CHECK(v == "123");
  CHECK(buf[3] == '\0');
}

TEST_CASE("format_into: exact fit is not truncated and stays terminated", "[fixed_format]") {
  std::array<char, 5> buf{};
  const auto v = format_into(buf, "%s", "1234");
  CHECK(v == "1234");
  CHECK(v.size() == 4);
  CHECK(buf[4] == '\0');
}

TEST_CASE("format_into: empty result yields an empty view and a terminated buffer", "[fixed_format]") {
  std::array<char, 8> buf{};
  buf.fill('#');
  const auto v = format_into(buf, "%s", "");
  CHECK(v.empty());
  CHECK(buf[0] == '\0');
}

TEST_CASE("format_into: returned view aliases the buffer", "[fixed_format]") {
  std::array<char, 8> buf{};
  const auto v = format_into(buf, "%d", 42);
  CHECK(v.data() == buf.data());
  CHECK(v == "42");
}

TEST_CASE("format_into: minimum two-byte buffer holds one character", "[fixed_format]") {
  std::array<char, 2> buf{};
  const auto v = format_into(buf, "%s", "x");
  CHECK(v == "x");
  CHECK(buf[1] == '\0');
}

}  // namespace
