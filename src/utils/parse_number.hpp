// Allocation-free number parsing for tokens from the wire and from CSV rows. Whole-token
// matches only: trailing characters are a failure, not a partial parse. C locale assumed
// ('.' is the decimal point), which is what the MCU and the recorder emit.
#pragma once

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iterator>
#include <string_view>
#include <system_error>

namespace c3::utils {

/// Parses a decimal or scientific-notation float token.
///
/// NaN, infinity and magnitudes beyond float range are rejected: a value that cannot be drawn
/// on a gauge is corrupt data, not a number. `out` is written only on success.
///
/// @param tok the whole token; must be shorter than 32 characters
/// @param out receives the value
/// @return true if the entire token parsed to a finite float; otherwise false
[[nodiscard]] inline bool parse_float(std::string_view tok, float& out) noexcept {
  std::array<char, 32> buf{};  // strtod needs a terminator; tokens are short
  if (tok.empty() || tok.size() >= buf.size()) {
    return false;
  }

  std::copy(tok.begin(), tok.end(), buf.begin());
  char* end = nullptr;
  const double v = std::strtod(buf.data(), &end);
  if (end != std::next(buf.data(), static_cast<std::ptrdiff_t>(tok.size()))) {
    return false;
  }

  // Largest double that still rounds to a finite float: FLT_MAX plus half an ulp. At or above
  // it the cast yields inf (the tie rounds to even, which is inf here).
  constexpr double kFloatOverflow = 0x1.ffffffp127;
  if (!std::isfinite(v) || std::fabs(v) >= kFloatOverflow) {
    return false;  // nan, inf, or a magnitude that would turn into inf on the float cast
  }

  out = static_cast<float>(v);
  return true;
}

/// Parses a base-10 integer token into any integer type.
///
/// No sign prefix other than '-', no whitespace, no decimal point. Overflow for the target
/// type is a failure. `out` is written only on success.
///
/// @param tok the whole token
/// @param out receives the value
/// @return true if the entire token parsed and fits in `Int`; otherwise false
template <typename Int>
[[nodiscard]] inline bool parse_int(std::string_view tok, Int& out) noexcept {
  const char* const last = std::next(tok.data(), static_cast<std::ptrdiff_t>(tok.size()));
  Int value{};
  const auto res = std::from_chars(tok.data(), last, value);
  if (res.ec != std::errc{} || res.ptr != last) {
    return false;
  }
  out = value;
  return true;
}

}  // namespace c3::utils
