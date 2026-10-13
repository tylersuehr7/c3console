#include "app/config.hpp"
#include "render/settings.hpp"
#include "render/units.hpp"
#include "utils/parse_enum_name.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

using c3::app::kAllSources;
using c3::app::Source;
using c3::render::kAllThemePreferences;
using c3::render::ThemePreference;
using c3::utils::parse_enum;

namespace {

TEST_CASE("parse_enum: matches to_string() of each value, at compile time", "[parse_enum_name]") {
  STATIC_CHECK(parse_enum("mock", kAllSources) == Source::Mock);
  STATIC_CHECK(parse_enum("replay", kAllSources) == Source::Replay);
  STATIC_CHECK(parse_enum("uart", kAllSources) == Source::Uart);
  STATIC_CHECK(parse_enum("auto", kAllThemePreferences) == ThemePreference::Auto);
  STATIC_CHECK(parse_enum("C", c3::render::kAllTempUnits) == c3::render::TempUnit::Celsius);
  STATIC_CHECK(parse_enum("bar", c3::render::kAllPressureUnits) == c3::render::PressureUnit::Bar);
}

TEST_CASE("parse_enum: is exact and case-sensitive", "[parse_enum_name]") {
  STATIC_CHECK_FALSE(parse_enum("Mock", kAllSources).has_value());
  STATIC_CHECK_FALSE(parse_enum("MOCK", kAllSources).has_value());
  STATIC_CHECK_FALSE(parse_enum(" mock", kAllSources).has_value());
  STATIC_CHECK_FALSE(parse_enum("mock ", kAllSources).has_value());
  STATIC_CHECK_FALSE(parse_enum("moc", kAllSources).has_value());
  STATIC_CHECK_FALSE(parse_enum("", kAllSources).has_value());
  STATIC_CHECK_FALSE(parse_enum("c", c3::render::kAllTempUnits).has_value());
}

TEST_CASE("parse_enum: only values listed in the array are found", "[parse_enum_name]") {
  constexpr std::array<Source, 2> kNoUart = {Source::Mock, Source::Replay};
  STATIC_CHECK(parse_enum("replay", kNoUart) == Source::Replay);
  STATIC_CHECK_FALSE(parse_enum("uart", kNoUart).has_value());
  constexpr std::array<Source, 0> kNone{};
  STATIC_CHECK_FALSE(parse_enum("mock", kNone).has_value());
}

TEST_CASE("parse_enum: the first listed value wins when names repeat", "[parse_enum_name]") {
  constexpr std::array<Source, 3> kRepeated = {Source::Uart, Source::Uart, Source::Mock};
  STATIC_CHECK(parse_enum("uart", kRepeated) == Source::Uart);
}

}  // namespace
