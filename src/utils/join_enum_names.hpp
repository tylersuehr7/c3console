#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace c3::utils {

/// "A, B, C" for messages. Allocates; init-time and error paths only.
template <typename E, std::size_t N>
[[nodiscard]] std::string join_names(const std::array<E, N>& all, std::string_view sep = ", ") {
  std::string out;
  for (const E value : all) {
    if (!out.empty()) {
      out += sep;
    }
    out += to_string(value);
  }
  return out;
}

}  // namespace c3::utils
