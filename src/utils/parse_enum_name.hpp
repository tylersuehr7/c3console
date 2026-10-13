#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace c3::utils {

/// Exact, case-sensitive match against to_string() of every value in `all`.
template <typename E, std::size_t N>
[[nodiscard]] constexpr std::optional<E> parse_enum(std::string_view text, const std::array<E, N>& all) noexcept {
  for (const E value : all) {
    if (text == to_string(value)) {
      return value;
    }
  }
  return std::nullopt;
}

}  // namespace c3::utils
