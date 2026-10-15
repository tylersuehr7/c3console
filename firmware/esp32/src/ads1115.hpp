// Minimal ADS1115 driver over Arduino Wire: single-shot conversions, differential or
// single-ended, ±4.096 V range (spec §3.3 front end). Only what the sampling loop needs;
// no library dependency to track.
#pragma once

#include <cstdint>

namespace hw {

enum class Mux : std::uint8_t {
  Diff01 = 0b000,  // AIN0 - AIN1
  Diff23 = 0b011,  // AIN2 - AIN3
  Single0 = 0b100,
  Single1 = 0b101,
  Single2 = 0b110,
  Single3 = 0b111,
};

enum class Rate : std::uint8_t {
  Sps8 = 0b000,
  Sps16 = 0b001,
  Sps32 = 0b010,
  Sps64 = 0b011,
  Sps128 = 0b100,
  Sps250 = 0b101,
};

class Ads1115 {
public:
  explicit Ads1115(std::uint8_t address) noexcept : address_(address) {}

  /// True when the chip answers on the bus.
  [[nodiscard]] bool probe() noexcept;
  /// Starts one conversion at ±4.096 V full scale. False on a bus error.
  [[nodiscard]] bool start(Mux mux, Rate rate) noexcept;
  /// True once the conversion in flight has finished.
  [[nodiscard]] bool ready() noexcept;
  /// Reads the last conversion. False on a bus error; `out` untouched then.
  [[nodiscard]] bool read(std::int16_t& out) noexcept;

  [[nodiscard]] std::uint8_t address() const noexcept { return address_; }

private:
  [[nodiscard]] bool write_register(std::uint8_t reg, std::uint16_t value) noexcept;
  [[nodiscard]] bool read_register(std::uint8_t reg, std::uint16_t& value) noexcept;

  std::uint8_t address_;
};

}  // namespace hw
