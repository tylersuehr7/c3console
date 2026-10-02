// snprintf into a fixed buffer. For numerals on the render path: no heap, safe truncation.
//
// Both helpers always leave `buf` NUL-terminated and return a view of exactly the characters
// written, so the result can go straight to NanoVG or the logger without a length lookup.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <string_view>

namespace c3::utils {

/// Formats printf-style into a fixed buffer, truncating rather than overflowing.
///
/// The buffer is always NUL-terminated afterwards, even on truncation or encoding error.
///
/// @param buf the destination; must hold at least one character plus the terminator
/// @param fmt a printf format string
/// @param args the values for `fmt`
/// @return a view of the text written into `buf`; empty if snprintf reported an error
template <std::size_t N, typename... Args>
[[nodiscard]] std::string_view format_into(std::array<char, N>& buf, const char* fmt, Args... args) noexcept {
  static_assert(N >= 2, "buffer must hold at least one character and the terminator");
  const int n = std::snprintf(buf.data(), N, fmt, args...);  // NOLINT(cppcoreguidelines-pro-type-vararg)
  if (n < 0) {
    buf[0] = '\0';
    return {};
  }
  const std::size_t len = std::min(static_cast<std::size_t>(n), N - 1);
  return {buf.data(), len};
}

/// Formats a number with a fixed count of decimals, like "%.{decimals}f".
///
/// @param buf the destination
/// @param value the number to format
/// @param decimals digits after the point; 0 gives a whole number
/// @return a view of the text written into `buf`, truncated if it did not fit
template <std::size_t N>
[[nodiscard]] std::string_view format_fixed(std::array<char, N>& buf, double value, int decimals) noexcept {
  return format_into(buf, "%.*f", decimals, value);
}

}  // namespace c3::utils
