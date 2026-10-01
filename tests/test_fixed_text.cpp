#include <catch2/catch_test_macros.hpp>
#include <string_view>

#include "utils/fixed_text.hpp"

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

}  // namespace