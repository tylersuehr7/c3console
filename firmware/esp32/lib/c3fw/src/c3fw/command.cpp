#include "c3fw/command.hpp"

#include "telemetry/protocol.hpp"
#include "utils/flat_json.hpp"
#include "utils/parse_number.hpp"

#include <array>
#include <cstddef>

namespace c3fw {
namespace {

using c3::utils::FlatJsonObject;
using c3::utils::JsonMember;
using c3::vehicle::HvacMode;

[[nodiscard]] bool parse_bool(const JsonMember& m, bool& out) noexcept {
  if (m.is_string) {
    return false;
  }

  if (m.value == "true") {
    out = true;
    return true;
  }

  if (m.value == "false") {
    out = false;
    return true;
  }

  return false;
}

struct Fields {
  Command cmd;
  bool has_cmd = false;
  bool has_blower = false;
  bool has_ac = false;
  bool has_mode = false;
  bool has_headlights = false;
  bool has_ch = false;
  bool has_action = false;
  bool has_raw = false;
  bool has_value = false;
  std::string_view action;
};

[[nodiscard]] std::string_view read_hvac_member(const JsonMember& m, Fields& f) noexcept {
  if (m.key == "blower") {
    std::uint32_t v = 0;
    if (m.is_string || !c3::utils::parse_int(m.value, v) || v > 4) {
      return "blower must be 0..4";
    }
    f.cmd.blower = static_cast<std::uint8_t>(v);
    f.has_blower = true;
  } else if (m.key == "ac") {
    if (!parse_bool(m, f.cmd.ac)) {
      return "ac must be true|false";
    }
    f.has_ac = true;
  } else if (m.key == "mode") {
    if (m.is_string && m.value == "vent") {
      f.cmd.mode = HvacMode::Vent;
    } else if (m.is_string && m.value == "floor") {
      f.cmd.mode = HvacMode::Floor;
    } else if (m.is_string && m.value == "defrost") {
      f.cmd.mode = HvacMode::Defrost;
    } else {
      return "mode must be vent|floor|defrost";
    }
    f.has_mode = true;
  }
  return {};
}

[[nodiscard]] std::string_view read_lamp_member(const JsonMember& m, Fields& f) noexcept {
  if (m.key == "headlights") {
    if (!parse_bool(m, f.cmd.headlights)) {
      return "headlights must be true|false";
    }
    f.has_headlights = true;
  } else if (m.key == "doors") {
    bool doors = false;
    if (!parse_bool(m, doors)) {
      return "doors must be true|false";
    }
    f.cmd.doors = doors;
  } else if (m.key == "high") {
    bool high = false;
    if (!parse_bool(m, high)) {
      return "high must be true|false";
    }
    f.cmd.high = high;
  }
  return {};
}

[[nodiscard]] std::string_view read_cal_member(const JsonMember& m, Fields& f) noexcept {
  if (m.key == "ch") {
    const auto ch = m.is_string ? c3::telemetry::parse_channel_key(m.value) : std::nullopt;
    if (!ch) {
      return "ch must be fuel|oil|clt|volt";
    }
    f.cmd.channel = *ch;
    f.has_ch = true;
  } else if (m.key == "action") {
    if (!m.is_string) {
      return "action must be a string";
    }
    f.action = m.value;
    f.has_action = true;
  } else if (m.key == "raw") {
    if (m.is_string || !c3::utils::parse_int(m.value, f.cmd.raw)) {
      return "raw must be an integer";
    }
    f.has_raw = true;
  } else if (m.key == "value") {
    if (m.is_string || !c3::utils::parse_float(m.value, f.cmd.value)) {
      return "value must be a number";
    }
    f.has_value = true;
  }
  return {};
}

// Reads one member into `f`; a recognised key with a bad value is an error, an unknown key is
// skipped. Keys are grouped by the command that uses them; the groups do not overlap.
[[nodiscard]] std::string_view read_member(const JsonMember& m, Fields& f) noexcept {
  if (m.key == "cmd") {
    if (!m.is_string) {
      return "cmd must be a string";
    }
    f.cmd.name = m.value;
    f.has_cmd = true;
    return {};
  }

  for (const auto reader : {&read_hvac_member, &read_lamp_member, &read_cal_member}) {
    if (const std::string_view err = reader(m, f); !err.empty()) {
      return err;
    }
  }

  return {};
}

// Checks the command has every field it needs and that the combination is one the relay
// board may carry out. Refusals here come back in the ack's "err" so the host can show them.
[[nodiscard]] std::string_view classify(Fields& f) noexcept {
  const std::string_view name = f.cmd.name;
  if (name == "hvac") {
    if (!f.has_blower || !f.has_ac || !f.has_mode) {
      return "hvac needs blower, ac and mode";
    }
    if (f.cmd.ac && f.cmd.blower == 0) {
      return "ac needs the blower on";  // spec §6.1: no compressor without airflow
    }
    f.cmd.kind = CommandKind::Hvac;
  } else if (name == "lamp") {
    if (!f.has_headlights) {
      return "lamp needs headlights";
    }
    f.cmd.kind = CommandKind::Lamp;
  } else if (name == "cal") {
    if (!f.has_ch || !f.has_action) {
      return "cal needs ch and action";
    }
    if (f.action == "add") {
      if (!f.has_raw || !f.has_value) {
        return "cal add needs raw and value";
      }
      f.cmd.kind = CommandKind::CalAdd;
    } else if (f.action == "save") {
      f.cmd.kind = CommandKind::CalSave;
    } else {
      return "cal action must be add|save";
    }
  } else if (name == "ping") {
    f.cmd.kind = CommandKind::Ping;
  } else if (name == "replay") {
    return "replay is played by the display host, not the MCU";
  } else {
    return "unknown cmd";
  }
  return {};
}

}  // namespace

CommandResult parse_command(std::string_view line) noexcept {
  FlatJsonObject obj(line);
  Fields f;
  JsonMember m;

  while (obj.next(m)) {
    if (const std::string_view err = read_member(m, f); !err.empty()) {
      return {.command = std::nullopt, .error = err, .name = f.cmd.name};
    }
  }

  if (obj.failed() || !obj.complete()) {
    return {.command = std::nullopt, .error = "malformed json", .name = f.cmd.name};
  }

  if (!f.has_cmd) {
    return {.command = std::nullopt, .error = "missing cmd", .name = {}};
  }

  if (const std::string_view err = classify(f); !err.empty()) {
    return {.command = std::nullopt, .error = err, .name = f.cmd.name};
  }

  return {.command = f.cmd, .error = {}, .name = f.cmd.name};
}

}  // namespace c3fw
