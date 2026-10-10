#pragma once

#include <chrono>
#include <cstdint>

namespace c3::utils {

/// Returns milliseconds on a monotonic timeline. Only differences are meaningful.
using MonotonicClock = std::uint64_t (*)() noexcept;

[[nodiscard]] inline std::uint64_t steady_now_ms() noexcept {
  using namespace std::chrono;
  return static_cast<std::uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

}  // namespace c3::utils
