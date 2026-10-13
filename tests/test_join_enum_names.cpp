#include "app/config.hpp"
#include "render/settings.hpp"
#include "utils/join_enum_names.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

namespace {

TEST_CASE("join_names: joins to_string() of every value with the default separator", "[join_enum_names]") {
  CHECK(c3::utils::join_names(c3::render::kAllThemePreferences) == "dark, light, auto");
  CHECK(c3::utils::join_names(c3::app::kAllSources) == "mock, replay, uart");
  CHECK(c3::utils::join_names(c3::app::kAllBackends) == "desktop, drm");
}

TEST_CASE("join_names: a custom separator is placed between values only", "[join_enum_names]") {
  CHECK(c3::utils::join_names(c3::app::kAllSources, "|") == "mock|replay|uart");
  CHECK(c3::utils::join_names(c3::app::kAllSources, "") == "mockreplayuart");
}

TEST_CASE("join_names: a single value has no separator and an empty array joins to nothing", "[join_enum_names]") {
  constexpr std::array<c3::app::Source, 1> kOne = {c3::app::Source::Uart};
  CHECK(c3::utils::join_names(kOne, "|") == "uart");
  constexpr std::array<c3::app::Source, 0> kNone{};
  CHECK(c3::utils::join_names(kNone).empty());
}

}  // namespace
