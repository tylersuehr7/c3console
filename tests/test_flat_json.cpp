#include "utils/flat_json.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using c3::utils::FlatJsonObject;
using c3::utils::JsonMember;

namespace {

/// Reads members until next() returns false; returns how many were read.
int drain(FlatJsonObject& object) noexcept {
  JsonMember member;
  int count = 0;
  while (object.next(member)) {
    ++count;
  }
  return count;
}

TEST_CASE("FlatJsonObject: test that flattened JSON string parses successfully", "[flat_json]") {
  auto object = FlatJsonObject(R"({"name":"hvac","blower":2,"ac":true,"doors":false})");
  JsonMember member;

  CHECK(object.next(member));
  CHECK(member.key == "name");
  CHECK(member.value == "hvac");
  CHECK(member.is_string == true);

  CHECK(object.next(member));
  CHECK(member.key == "blower");
  CHECK(member.value == "2");
  CHECK(member.is_string == false);

  CHECK(object.next(member));
  CHECK(member.key == "ac");
  CHECK(member.value == "true");
  CHECK(member.is_string == false);

  CHECK(object.next(member));
  CHECK(member.key == "doors");
  CHECK(member.value == "false");
  CHECK(member.is_string == false);

  CHECK_FALSE(object.next(member));
  CHECK(object.complete());
  CHECK_FALSE(object.failed());
}

TEST_CASE("FlatJsonObject: test that empty object parses with no members", "[flat_json]") {
  auto object = FlatJsonObject("{}");
  CHECK(drain(object) == 0);
  CHECK(object.complete());
  CHECK_FALSE(object.failed());
}

TEST_CASE("FlatJsonObject: test that spaces, tabs and carriage returns are skipped between tokens", "[flat_json]") {
  auto object = FlatJsonObject(" \t{ \"a\" : 1 ,\t\"b\" : \"x\" }\r");
  JsonMember member;

  CHECK(object.next(member));
  CHECK(member.key == "a");
  CHECK(member.value == "1");
  CHECK(member.is_string == false);

  CHECK(object.next(member));
  CHECK(member.key == "b");
  CHECK(member.value == "x");
  CHECK(member.is_string == true);

  CHECK_FALSE(object.next(member));
  CHECK(object.complete());
  CHECK_FALSE(object.failed());
}

TEST_CASE("FlatJsonObject: test that numeric values keep sign and decimal point", "[flat_json]") {
  auto object = FlatJsonObject(R"({"n":-12.5})");
  JsonMember member;

  CHECK(object.next(member));
  CHECK(member.key == "n");
  CHECK(member.value == "-12.5");
  CHECK(member.is_string == false);
  CHECK_FALSE(object.next(member));
  CHECK(object.complete());
}

TEST_CASE("FlatJsonObject: test that empty string value is accepted and flagged as string", "[flat_json]") {
  auto object = FlatJsonObject(R"({"a":""})");
  JsonMember member;

  CHECK(object.next(member));
  CHECK(member.key == "a");
  CHECK(member.value.empty());
  CHECK(member.is_string == true);
  CHECK_FALSE(object.next(member));
  CHECK(object.complete());
  CHECK_FALSE(object.failed());
}

TEST_CASE("FlatJsonObject: test that nested JSON string fails to parse", "[flat_json]") {
  auto object = FlatJsonObject(R"({"a":{"b":1}})");
  drain(object);
  CHECK_FALSE(object.complete());
}

TEST_CASE("FlatJsonObject: test that nested object followed by another member fails to parse", "[flat_json]") {
  auto object = FlatJsonObject(R"({"a":{"b":1},"c":2})");
  drain(object);
  CHECK_FALSE(object.complete());
}

TEST_CASE("FlatJsonObject: test that array value fails to parse", "[flat_json]") {
  auto object = FlatJsonObject(R"({"a":[1,2]})");
  drain(object);
  CHECK(object.failed());
  CHECK_FALSE(object.complete());
}

TEST_CASE("FlatJsonObject: test that missing opening brace fails", "[flat_json]") {
  auto object = FlatJsonObject(R"("a":1})");
  JsonMember member;
  CHECK_FALSE(object.next(member));
  CHECK(object.failed());
  CHECK_FALSE(object.complete());
}

TEST_CASE("FlatJsonObject: test that empty input fails", "[flat_json]") {
  auto object = FlatJsonObject("");
  JsonMember member;
  CHECK_FALSE(object.next(member));
  CHECK(object.failed());
  CHECK_FALSE(object.complete());
}

TEST_CASE("FlatJsonObject: test that missing closing brace fails", "[flat_json]") {
  auto object = FlatJsonObject(R"({"a":1)");
  JsonMember member;
  CHECK(object.next(member));
  CHECK(member.value == "1");
  CHECK_FALSE(object.next(member));
  CHECK(object.failed());
  CHECK_FALSE(object.complete());
}

TEST_CASE("FlatJsonObject: test that unterminated string value fails", "[flat_json]") {
  auto object = FlatJsonObject(R"({"a":"x)");
  JsonMember member;
  CHECK_FALSE(object.next(member));
  CHECK(object.failed());
  CHECK_FALSE(object.complete());
}

TEST_CASE("FlatJsonObject: test that unquoted key fails", "[flat_json]") {
  auto object = FlatJsonObject(R"({a:1})");
  JsonMember member;
  CHECK_FALSE(object.next(member));
  CHECK(object.failed());
}

TEST_CASE("FlatJsonObject: test that missing colon fails", "[flat_json]") {
  auto object = FlatJsonObject(R"({"a" 1})");
  JsonMember member;
  CHECK_FALSE(object.next(member));
  CHECK(object.failed());
}

TEST_CASE("FlatJsonObject: test that missing value fails", "[flat_json]") {
  auto object = FlatJsonObject(R"({"a":})");
  JsonMember member;
  CHECK_FALSE(object.next(member));
  CHECK(object.failed());
}

TEST_CASE("FlatJsonObject: test that trailing comma fails", "[flat_json]") {
  auto object = FlatJsonObject(R"({"a":1,})");
  JsonMember member;
  CHECK(object.next(member));
  CHECK_FALSE(object.next(member));
  CHECK(object.failed());
  CHECK_FALSE(object.complete());
}

TEST_CASE("FlatJsonObject: test that missing comma between members fails", "[flat_json]") {
  auto object = FlatJsonObject(R"({"a":1 "b":2})");
  JsonMember member;
  CHECK(object.next(member));
  CHECK_FALSE(object.next(member));
  CHECK(object.failed());
}

TEST_CASE("FlatJsonObject: test that escape sequences in strings are rejected", "[flat_json]") {
  auto object = FlatJsonObject(R"({"a":"x\"y"})");
  JsonMember member;
  CHECK_FALSE(object.next(member));
  CHECK(object.failed());
}

TEST_CASE("FlatJsonObject: test that trailing garbage after closing brace is not complete", "[flat_json]") {
  auto object = FlatJsonObject(R"({"a":1}x)");
  JsonMember member;
  CHECK(object.next(member));
  CHECK_FALSE(object.next(member));
  CHECK_FALSE(object.failed());
  CHECK_FALSE(object.complete());
}

TEST_CASE("FlatJsonObject: test that a second object after the first is not complete", "[flat_json]") {
  auto object = FlatJsonObject(R"({"a":1}{"b":2})");
  CHECK(drain(object) == 1);
  CHECK_FALSE(object.complete());
}

}  // namespace
