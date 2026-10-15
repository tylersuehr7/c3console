// Median-of-5 spike rejection (spec §3.4): the first stage after the ADC, before the IIR.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace c3fw {

[[nodiscard]] constexpr std::int16_t median5(std::array<std::int16_t, 5> s) noexcept {
  std::sort(s.begin(), s.end());
  return s[2];
}

}  // namespace c3fw
