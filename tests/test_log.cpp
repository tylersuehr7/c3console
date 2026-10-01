#include <catch2/catch_test_macros.hpp>

#include "logging/log.hpp"

namespace l = c3::logging;

namespace {

TEST_CASE("to_string: tests that Level::Debug returns 'debug'", "[log]") {
  const auto level = l::Level::Debug;
  const auto result = l::to_string(level);
  CHECK("debug" == result);
}

TEST_CASE("to_string: tests that Level::Info returns 'info'", "[log]") {
  const auto level = l::Level::Info;
  const auto result = l::to_string(level);
  CHECK("info" == result);
}

TEST_CASE("to_string: tests that Level::Warn returns 'warn'", "[log]") {
  const auto level = l::Level::Warn;
  const auto result = l::to_string(level);
  CHECK("warn" == result);
}

TEST_CASE("to_string: tests that Level::Error returns 'error'", "[log]") {
  const auto level = l::Level::Error;
  const auto result = l::to_string(level);
  CHECK("error" == result);
}

TEST_CASE("to_priority_prefix: tests that Level::Debug returns '<7>'", "[log]") {
  const auto level = l::Level::Debug;
  const auto result = l::to_priority_prefix(level);
  CHECK("<7>" == result);
}

TEST_CASE("to_priority_prefix: tests that Level::Info returns '<6>'", "[log]") {
  const auto level = l::Level::Info;
  const auto result = l::to_priority_prefix(level);
  CHECK("<6>" == result);
}

TEST_CASE("to_priority_prefix: tests that Level::Warn returns '<4>'", "[log]") {
  const auto level = l::Level::Warn;
  const auto result = l::to_priority_prefix(level);
  CHECK("<4>" == result);
}

TEST_CASE("to_priority_prefix: tests that Level::Error returns '<3>'", "[log]") {
  const auto level = l::Level::Error;
  const auto result = l::to_priority_prefix(level);
  CHECK("<3>" == result);
}

TEST_CASE("write: tests that no write occurs when below current level", "[log]") {
  const auto level = l::Level::Debug;
  l::set_level(l::Level::Info);
  const auto result = l::write(level, "test", "Something");
  CHECK(false == result);
}

TEST_CASE("write: tests that write occurs when at current level", "[log]") {
  const auto level = l::Level::Info;
  l::set_level(l::Level::Info);
  const auto result = l::write(level, "test", "Something");
  CHECK(true == result);
}

TEST_CASE("write: tests that write occurs when above current level", "[log]") {
  const auto level = l::Level::Warn;
  l::set_level(l::Level::Info);
  const auto result = l::write(level, "test", "Something");
  CHECK(true == result);
}

}  // namespace