#include "telemetry/crc16.hpp"

#include <catch2/catch_test_macros.hpp>

using c3::telemetry::Crc16Ccitt;

namespace {

TEST_CASE("CRC-16/CCITT-FALSE: known vectors", "[crc]") {
  CHECK(Crc16Ccitt::compute("") == 0xFFFF);
  CHECK(Crc16Ccitt::compute("A") == 0xB915);
  CHECK(Crc16Ccitt::compute("123456789") == 0x29B1);
  CHECK(Crc16Ccitt::compute("The quick brown fox jumps over the lazy dog") == 0x8FDD);
}

TEST_CASE("CRC-16/CCITT-FALSE: span-wise update equals one-shot", "[crc]") {
  Crc16Ccitt c;
  c.update("12345");
  c.update("6789");
  CHECK(c.value() == 0x29B1);
  Crc16Ccitt d;
  for (const char ch : std::string_view("123456789")) {
    d.update(static_cast<std::uint8_t>(ch));
  }
  CHECK(d.value() == 0x29B1);
}

TEST_CASE("CRC-16/CCITT-FALSE: a single flipped bit changes the sum", "[crc]") {
  CHECK(Crc16Ccitt::compute("{\"t\":1}") != Crc16Ccitt::compute("{\"t\":0}"));
}

}  // namespace
