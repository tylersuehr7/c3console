#include <catch2/catch_test_macros.hpp>

#include "vehicle/vehicle_types.hpp"

namespace {

TEST_CASE("to_string: test that HvacMode::Vent returns 'vent'", "[vehicle_types]") {
  const auto mode = c3::vehicle::HvacMode::Vent;
  CHECK(c3::vehicle::to_string(mode) == "vent");
}

TEST_CASE("to_string: test that HvacMode::Floor returns 'floor'", "[vehicle_types]") {
  const auto mode = c3::vehicle::HvacMode::Floor;
  CHECK(c3::vehicle::to_string(mode) == "floor");
}

TEST_CASE("to_string: test that HvacMode::Defrost returns 'defrost'", "[vehicle_types]") {
  const auto mode = c3::vehicle::HvacMode::Defrost;
  CHECK(c3::vehicle::to_string(mode) == "defrost");
}

TEST_CASE("is_valid: test that HvacState is invalid when A/C is on but blower is off", "[vehicle_types]") {
  const auto hvac = c3::vehicle::HvacState{
      .blower = c3::vehicle::HvacBlower::Off,
      .ac_enabled = true,
  };
  CHECK(c3::vehicle::is_valid(hvac) == false);
}

TEST_CASE("is_valid: test that HvacState is valid when A/C is on and blower is on", "[vehicle_types]") {
  const auto hvac = c3::vehicle::HvacState{
      .blower = c3::vehicle::HvacBlower::L1,
      .ac_enabled = true,
  };
  CHECK(c3::vehicle::is_valid(hvac) == true);
}

TEST_CASE("is_valid: test that HvacState is valid when A/C is off and blower is on", "[vehicle_types]") {
  const auto hvac = c3::vehicle::HvacState{
      .blower = c3::vehicle::HvacBlower::L1,
      .ac_enabled = false,
  };
  CHECK(c3::vehicle::is_valid(hvac) == true);
}

TEST_CASE("is_valid: test that HvacState is valid when A/C is off and blower is off", "[vehicle_types]") {
  const auto hvac = c3::vehicle::HvacState{
      .blower = c3::vehicle::HvacBlower::Off,
      .ac_enabled = false,
  };
  CHECK(c3::vehicle::is_valid(hvac) == true);
}

TEST_CASE("is_valid: test that LampState is invalid when highbeams on but headlights are off", "[vehicle_types]") {
  const auto lamp = c3::vehicle::LampState{
      .headlights_on = false,
      .highbeams_on = true,
  };
  CHECK(c3::vehicle::is_valid(lamp) == false);
}

TEST_CASE("is_valid: test that LampState is invalid when headlights on but lamp doors are closed", "[vehicle_types]") {
  const auto lamp = c3::vehicle::LampState{
      .lamp_doors_open = false,
      .headlights_on = true,
  };
  CHECK(c3::vehicle::is_valid(lamp) == false);
}

TEST_CASE("is_valid: test that LampState is valid when headlights on and lamp doors are opened", "[vehicle_types]") {
  const auto lamp = c3::vehicle::LampState{
      .lamp_doors_open = true,
      .headlights_on = true,
  };
  CHECK(c3::vehicle::is_valid(lamp) == true);
}

TEST_CASE("is_valid: test that LampState is valid when highbeams on and headlights are on", "[vehicle_types]") {
  const auto lamp = c3::vehicle::LampState{
      .lamp_doors_open = true,
      .headlights_on = true,
      .highbeams_on = true,
  };
  CHECK(c3::vehicle::is_valid(lamp) == true);
}

}  // namespace