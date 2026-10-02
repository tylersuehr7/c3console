// snprintf into a fixed buffer. For numerals on the render path: no heap, safe truncation.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <string_view>

namespace c3::utils {

/// Formats into `buf`, always NUL-terminated, truncated if needed. Returns the text written.
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

/// "%.{decimals}f"
template <std::size_t N>
[[nodiscard]] std::string_view format_fixed(std::array<char, N>& buf, double value, int decimals) noexcept {
  return format_into(buf, "%.*f", decimals, value);
}

}  // namespace c3::utils
