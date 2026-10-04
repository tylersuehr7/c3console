// CRC-16/CCITT-FALSE: polynomial 0x1021, init 0xFFFF, no reflection, no final xor.
// Check value: crc("123456789") == 0x29B1.
//
// This is the variant the spec means by "CRC-16/CCITT"; firmware must use the same
// one. Constexpr and span-wise so message can be checksummed in pieces without
// copying.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace c3::telemetry {

class Crc16Ccitt {
public:
  static constexpr std::uint16_t kInit = 0xFFFF;
  static constexpr std::uint16_t kPoly = 0x1021;

  constexpr void update(std::uint8_t byte) noexcept {
    crc_ ^= static_cast<std::uint16_t>(byte) << 8U;
    for (int i = 0; i < 8; ++i) {
      const bool msb = (crc_ & 0x8000U) != 0;
      crc_ = static_cast<std::uint16_t>(crc_ << 1U);
      if (msb) {
        crc_ ^= kPoly;
      }
    }
  }

  constexpr void update(std::string_view bytes) noexcept {
    for (const char c : bytes) {
      update(static_cast<std::uint8_t>(c));
    }
  }

  [[nodiscard]] constexpr std::uint16_t value() const noexcept { return crc_; }

  [[nodiscard]] static constexpr std::uint16_t compute(std::string_view bytes) noexcept {
    Crc16Ccitt c;
    c.update(bytes);
    return c.value();
  }

private:
  std::uint16_t crc_ = kInit;
};

static_assert(Crc16Ccitt::compute("123456789") == 0x29B1);

}  // namespace c3::telemetry
