#include "ads1115.hpp"

#include <Wire.h>

namespace hw {
namespace {

constexpr std::uint8_t kRegConversion = 0x00;
constexpr std::uint8_t kRegConfig = 0x01;

constexpr std::uint16_t kOsStart = 1U << 15U;     // begin single conversion / not busy
constexpr std::uint16_t kPga4096 = 0b001U << 9U;  // ±4.096 V
constexpr std::uint16_t kModeSingle = 1U << 8U;
constexpr std::uint16_t kCompDisable = 0b11U;  // comparator off

}  // namespace

bool Ads1115::probe() noexcept {
  std::uint16_t cfg = 0;
  return read_register(kRegConfig, cfg);
}

bool Ads1115::start(Mux mux, Rate rate) noexcept {
  const std::uint16_t cfg = kOsStart | (static_cast<std::uint16_t>(mux) << 12U) | kPga4096 | kModeSingle |
                            (static_cast<std::uint16_t>(rate) << 5U) | kCompDisable;
  return write_register(kRegConfig, cfg);
}

bool Ads1115::ready() noexcept {
  std::uint16_t cfg = 0;
  return read_register(kRegConfig, cfg) && (cfg & kOsStart) != 0;
}

bool Ads1115::read(std::int16_t& out) noexcept {
  std::uint16_t raw = 0;
  if (!read_register(kRegConversion, raw)) {
    return false;
  }
  out = static_cast<std::int16_t>(raw);
  return true;
}

bool Ads1115::write_register(std::uint8_t reg, std::uint16_t value) noexcept {
  Wire.beginTransmission(address_);
  Wire.write(reg);
  Wire.write(static_cast<std::uint8_t>(value >> 8U));
  Wire.write(static_cast<std::uint8_t>(value & 0xFFU));
  return Wire.endTransmission() == 0;
}

bool Ads1115::read_register(std::uint8_t reg, std::uint16_t& value) noexcept {
  Wire.beginTransmission(address_);
  Wire.write(reg);

  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  if (Wire.requestFrom(static_cast<int>(address_), 2) != 2) {
    return false;
  }

  const int hi = Wire.read();
  const int lo = Wire.read();
  if (hi < 0 || lo < 0) {
    return false;
  }

  value = static_cast<std::uint16_t>((hi << 8) | lo);
  return true;
}

}  // namespace hw
