#include "utils/fixed_text.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string_view>

namespace {

TEST_CASE("append: tests that appending std::string_view works", "[FixedText]") {
  auto text = c3::utils::FixedText<16>();
  std::string_view value = std::string_view("ABC123");
  text.append(value);
  CHECK("ABC123" == text.view());
}

TEST_CASE("append: tests that appending char works", "[FixedText]") {
  auto text = c3::utils::FixedText<16>();
  char value = 'a';
  text.append(value);
  CHECK("a" == text.view());
}

TEST_CASE("append: tests that appending C-String works", "[FixedText]") {
  auto text = c3::utils::FixedText<16>();
  const char* value = "Something";
  text.append(value);
  CHECK("Something" == text.view());
}

TEST_CASE("append: tests that appending boolean true works", "[FixedText]") {
  auto text = c3::utils::FixedText<16>();
  bool value = true;
  text.append(value);
  CHECK("true" == text.view());
}

TEST_CASE("append: tests that appending boolean false works", "[FixedText]") {
  auto text = c3::utils::FixedText<16>();
  bool value = false;
  text.append(value);
  CHECK("false" == text.view());
}

TEST_CASE("append: tests that appending int works", "[FixedText]") {
  auto text = c3::utils::FixedText<16>();
  int value = 32;
  text.append(value);
  CHECK("32" == text.view());
}

TEST_CASE("append: tests that appending long works", "[FixedText]") {
  auto text = c3::utils::FixedText<16>();
  long value = 32;
  text.append(value);
  CHECK("32" == text.view());
}

TEST_CASE("append: tests that appending float works", "[FixedText]") {
  auto text = c3::utils::FixedText<16>();
  float value = 0.62;
  text.append(value);
  CHECK("0.62" == text.view());
}

TEST_CASE("append: tests that appending double works", "[FixedText]") {
  auto text = c3::utils::FixedText<16>();
  double value = 32.33253;
  text.append(value);
  CHECK("32.33253" == text.view());
}

TEST_CASE("reset: tests that resets array with 0", "[FixedText]") {
  auto text = c3::utils::FixedText<16>();
  text.append("SUEHR");
  CHECK(5 == text.size());
  auto previous_size = text.reset();
  CHECK(0 == text.size());
  CHECK("" == text.view());
  CHECK(5 == previous_size);
}

TEST_CASE("append: tests that appending too much data truncates it", "[FixedText]") {
  auto text = c3::utils::FixedText<16>();
  text.append("1234567890ABCDEFGHIJ");
  CHECK(16 == text.size());
  CHECK("1234567890ABCDEF" == text.view());
}

TEST_CASE("append: tests that appending null C-String writes '(null)' instead of dereferencing", "[FixedText]") {
  auto text = c3::utils::FixedText<16>();
  const char* value = nullptr;
  text.append(value);
  CHECK("(null)" == text.view());
}

TEST_CASE("append: tests that appending negative int works", "[FixedText]") {
  auto text = c3::utils::FixedText<16>();
  int value = -17;
  text.append(value);
  CHECK("-17" == text.view());
}

TEST_CASE("append: tests that appending zero works", "[FixedText]") {
  auto text = c3::utils::FixedText<16>();
  int value = 0;
  text.append(value);
  CHECK("0" == text.view());
}

TEST_CASE("append: tests that successive appends concatenate in order", "[FixedText]") {
  auto text = c3::utils::FixedText<32>();
  text.append("blower=");
  text.append(2);
  text.append(' ');
  text.append("ac=");
  text.append(true);
  CHECK("blower=2 ac=true" == text.view());
  CHECK(16 == text.size());
}

TEST_CASE("append: tests that filling exactly to capacity keeps every character", "[FixedText]") {
  auto text = c3::utils::FixedText<4>();
  text.append("ABCD");
  CHECK(4 == text.size());
  CHECK("ABCD" == text.view());
}

TEST_CASE("append: tests that a later append truncates at the remaining capacity", "[FixedText]") {
  auto text = c3::utils::FixedText<8>();
  text.append("12345");
  text.append("6789");
  CHECK(8 == text.size());
  CHECK("12345678" == text.view());
}

TEST_CASE("append: tests that appending to a full buffer is a no-op", "[FixedText]") {
  auto text = c3::utils::FixedText<4>();
  text.append("ABCD");
  text.append("EFGH");
  text.append('Z');
  text.append(99);
  text.append(true);
  CHECK(4 == text.size());
  CHECK("ABCD" == text.view());
}

TEST_CASE("append: tests that a number longer than the remaining capacity is truncated", "[FixedText]") {
  auto text = c3::utils::FixedText<4>();
  text.append(123456);
  CHECK(4 == text.size());
  CHECK("1234" == text.view());
}

TEST_CASE("reset: tests that appending after reset starts from the beginning", "[FixedText]") {
  auto text = c3::utils::FixedText<16>();
  text.append("first");
  static_cast<void>(text.reset());
  text.append("second");
  CHECK(6 == text.size());
  CHECK("second" == text.view());
}

TEST_CASE("reset: tests that reset on an empty buffer returns 0", "[FixedText]") {
  auto text = c3::utils::FixedText<16>();
  CHECK(0 == text.reset());
  CHECK(0 == text.size());
}

TEST_CASE("view: tests that a fresh buffer is empty", "[FixedText]") {
  const auto text = c3::utils::FixedText<16>();
  CHECK(0 == text.size());
  CHECK(text.view().empty());
}

}  // namespace
