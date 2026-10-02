#include "vehicle_commands.hpp"

#include <array>

#include "utils/fixed_format.hpp"
#include "utils/flat_json.hpp"

namespace c3::vehicle {

namespace {
[[nodiscard]] std::size_t copy_out(std::string_view text, std::span<char> out) noexcept {
  if (text.empty() || out.size() < text.size()) {
    return 0;
  }
  std::copy(text.begin(), text.end(), out.begin());
  return text.size();
}
}  // namespace

std::size_t format_hvac_command(const HvacState& state, std::span<char> out) noexcept {
  std::array<char, kMaxCommandLength + 1> buffer{};

  const auto text = utils::format_into(buffer, R"({"cmd":"hvac","blower":%u,"ac":%s,"mode":"%s"})",
                                       static_cast<unsigned>(state.blower), state.ac_enabled ? "true" : "false",
                                       to_string(state.mode).data());

  return copy_out(text, out);
}

std::size_t format_lamp_command(const LampState& state, std::span<char> out) noexcept {
  std::array<char, kMaxCommandLength + 1> buffer{};

  const auto text = utils::format_into(buffer, R"({"cmd":"lamp","headlights":%s,"doors":%s,"high":%s})",
                                       state.headlights_on ? "true" : "false", state.lamp_doors_open ? "true" : "false",
                                       state.highbeams_on ? "true" : "false");

  return copy_out(text, out);
}

std::optional<Acknowledgement> parse_acknowledgement(std::string_view line) noexcept {
  utils::FlatJsonObject object(line);
  utils::JsonMember json;
  std::optional<CommandKind> kind;
  std::optional<bool> ok;

  while (object.next(json)) {
    if (json.key == "ack") {
      if (kind || !json.is_string) {
        return std::nullopt;
      }

      if (json.value == "hvac") {
        kind = CommandKind::Hvac;
      } else if (json.value == "lamp") {
        kind = CommandKind::Lamp;
      } else {
        return std::nullopt;
      }
    } else if (json.key == "ok") {
      if (ok || json.is_string) {
        return std::nullopt;
      }

      if (json.value == "true") {
        ok = true;
      } else if (json.value == "false") {
        ok = false;
      } else {
        return std::nullopt;
      }
    }
  }

  if (!object.complete() || !kind || !ok) {
    return std::nullopt;
  }

  return Acknowledgement{.kind = *kind, .ok = *ok};
}

}  // namespace c3::vehicle
