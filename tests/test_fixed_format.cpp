#include <array>
#include <catch2/catch_test_macros.hpp>

#include "utils/fixed_format.hpp"

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

}  // namespace