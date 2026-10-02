#include "vehicle/vehicle_types.hpp"

#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("is_valid: test that default HvacState is valid", "[vehicle_types]") {
  const auto hvac = c3::vehicle::HvacState{};
  CHECK(c3::vehicle::is_valid(hvac) == true);
}

TEST_CASE("is_valid: test that HvacState is valid with A/C on at every non-off blower level", "[vehicle_types]") {
  for (const auto blower : {c3::vehicle::HvacBlower::L1, c3::vehicle::HvacBlower::L2, c3::vehicle::HvacBlower::L3,
                            c3::vehicle::HvacBlower::L4}) {
    const auto hvac = c3::vehicle::HvacState{.blower = blower, .ac_enabled = true};
    CHECK(c3::vehicle::is_valid(hvac) == true);
  }
}

TEST_CASE("is_valid: test that HvacState A/C rule is independent of mode", "[vehicle_types]") {
  for (const auto mode : {c3::vehicle::HvacMode::Vent, c3::vehicle::HvacMode::Floor, c3::vehicle::HvacMode::Defrost}) {
    const auto off = c3::vehicle::HvacState{.mode = mode, .blower = c3::vehicle::HvacBlower::Off, .ac_enabled = true};
    const auto on = c3::vehicle::HvacState{.mode = mode, .blower = c3::vehicle::HvacBlower::L1, .ac_enabled = true};
    CHECK(c3::vehicle::is_valid(off) == false);
    CHECK(c3::vehicle::is_valid(on) == true);
  }
}

TEST_CASE("is_valid: test that default LampState is valid", "[vehicle_types]") {
  const auto lamp = c3::vehicle::LampState{};
  CHECK(c3::vehicle::is_valid(lamp) == true);
}

TEST_CASE("is_valid: test that LampState is valid when only lamp doors are opened", "[vehicle_types]") {
  const auto lamp = c3::vehicle::LampState{.lamp_doors_open = true};
  CHECK(c3::vehicle::is_valid(lamp) == true);
}

TEST_CASE("is_valid: test that LampState is invalid when highbeams on with lamp doors open but headlights off",
          "[vehicle_types]") {
  const auto lamp = c3::vehicle::LampState{
      .lamp_doors_open = true,
      .headlights_on = false,
      .highbeams_on = true,
  };
  CHECK(c3::vehicle::is_valid(lamp) == false);
}

TEST_CASE("is_valid: test that LampState is invalid when highbeams and headlights on but lamp doors are closed",
          "[vehicle_types]") {
  const auto lamp = c3::vehicle::LampState{
      .lamp_doors_open = false,
      .headlights_on = true,
      .highbeams_on = true,
  };
  CHECK(c3::vehicle::is_valid(lamp) == false);
}

TEST_CASE("operator==: test that HvacState compares every field", "[vehicle_types]") {
  const auto base = c3::vehicle::HvacState{
      .mode = c3::vehicle::HvacMode::Floor,
      .blower = c3::vehicle::HvacBlower::L2,
      .ac_enabled = true,
  };
  CHECK(base == c3::vehicle::HvacState{
                    .mode = c3::vehicle::HvacMode::Floor, .blower = c3::vehicle::HvacBlower::L2, .ac_enabled = true});
  CHECK(base != c3::vehicle::HvacState{
                    .mode = c3::vehicle::HvacMode::Vent, .blower = c3::vehicle::HvacBlower::L2, .ac_enabled = true});
  CHECK(base != c3::vehicle::HvacState{
                    .mode = c3::vehicle::HvacMode::Floor, .blower = c3::vehicle::HvacBlower::L3, .ac_enabled = true});
  CHECK(base != c3::vehicle::HvacState{
                    .mode = c3::vehicle::HvacMode::Floor, .blower = c3::vehicle::HvacBlower::L2, .ac_enabled = false});
}

TEST_CASE("operator==: test that LampState compares every field", "[vehicle_types]") {
  const auto base = c3::vehicle::LampState{
      .lamp_doors_open = true,
      .headlights_on = true,
      .highbeams_on = false,
  };
  CHECK(base == c3::vehicle::LampState{.lamp_doors_open = true, .headlights_on = true, .highbeams_on = false});
  CHECK(base != c3::vehicle::LampState{.lamp_doors_open = false, .headlights_on = true, .highbeams_on = false});
  CHECK(base != c3::vehicle::LampState{.lamp_doors_open = true, .headlights_on = false, .highbeams_on = false});
  CHECK(base != c3::vehicle::LampState{.lamp_doors_open = true, .headlights_on = true, .highbeams_on = true});
}

TEST_CASE("RelayState: test that defaults are all off and optional relays are absent", "[vehicle_types]") {
  const auto relays = c3::vehicle::RelayState{};
  CHECK(relays.blower == c3::vehicle::HvacBlower::Off);
  CHECK(relays.mode == c3::vehicle::HvacMode::Vent);
  CHECK(relays.ac_enabled == false);
  CHECK(relays.headlights_on == false);
  CHECK_FALSE(relays.lamp_doors_open.has_value());
  CHECK_FALSE(relays.highbeams_on.has_value());
}

}  // namespace
