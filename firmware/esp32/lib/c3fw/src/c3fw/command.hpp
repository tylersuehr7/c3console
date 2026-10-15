// Pi -> MCU command parser (spec §4.2). One flat JSON object per line:
//   {"cmd":"hvac","blower":2,"ac":true,"mode":"vent"}
//   {"cmd":"lamp","headlights":true,"doors":false,"high":false}
//   {"cmd":"cal","ch":"fuel","action":"add","raw":12843,"value":0.50}
//   {"cmd":"cal","ch":"fuel","action":"save"}
//   {"cmd":"ping"}
// Hand-rolled, allocation-free, exception-free. Unknown keys are ignored; a missing required
// key or a malformed value rejects the whole command so a relay never moves on half a message.
// `replay` is a Pi-side concern in this build and is rejected with an explanation.
#pragma once

#include "telemetry/frame.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace c3fw {

enum class CommandKind : std::uint8_t {
  Hvac,
  Lamp,
  CalAdd,
  CalSave,
  Ping,
};

struct Command {
  CommandKind kind = CommandKind::Ping;
  std::string_view name;  // the "cmd" value, echoed in the ack; points into the input line
  // hvac
  std::uint8_t blower = 0;
  bool ac = false;
  c3::vehicle::HvacMode mode = c3::vehicle::HvacMode::Vent;
  // lamp
  bool headlights = false;
  std::optional<bool> doors;  // absent when the Pi did not send it
  std::optional<bool> high;   // high beams, same rule
  // cal
  c3::telemetry::Channel channel = c3::telemetry::Channel::Fuel;
  std::int32_t raw = 0;
  float value = 0.0F;
};

struct CommandResult {
  std::optional<Command> command;  // nullopt: rejected
  std::string_view error;          // static text for the ack's "err"
  std::string_view name;           // "cmd" value if it could be read, for the ack
};

[[nodiscard]] CommandResult parse_command(std::string_view line) noexcept;

}  // namespace c3fw
