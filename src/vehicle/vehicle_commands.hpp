// HVAC and Lamp commands, Pi -> MCU:
//   {"cmd":"hvac","blower":2,"ac":true,"mode":"vent"}
//   {"cmd":"lamp","headlights":true,"doors":false,"high":false}
//
// Formatting is allocation-free so the UI thread can emit directly. The MCU acknowledges
// and re-asserts relay state in every frame; the UI displays that, never its own optimism,
// so it cannot drift from reality (MCU holds state on comms loss, not us).
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "vehicle_types.hpp"

namespace c3::vehicle {

/// Longest command line, without a terminator.
constexpr std::size_t kMaxCommandLength = 64;

/// Writes the HvacState to JSON line (no newline) into `out` for MCU.
/// @return the length, or 0 if `out` is too small
[[nodiscard]] std::size_t format_hvac_command(const HvacState& state, std::span<char> out) noexcept;

/// Writes the LampState to JSON line (no newline) into `out` for MCU.
/// @return the length, or 0 if `out` is too small
[[nodiscard]] std::size_t format_lamp_command(const LampState& state, std::span<char> out) noexcept;

enum class CommandKind : std::uint8_t { Hvac = 0, Lamp };
enum class SubmitResult : std::uint8_t { Accepted = 0, Busy, Unavailable, Invalid };
enum class CommandStatus : std::uint8_t { Idle = 0, Pending, Acknowledged, Confirmed, Rejected, TimedOut, Unavailable };

class CommandSink {
 public:
  virtual ~CommandSink() = default;
  CommandSink(const CommandSink&) = delete;
  CommandSink& operator=(const CommandSink&) = delete;
  CommandSink(CommandSink&&) = delete;
  CommandSink& operator=(CommandSink&&) = delete;
  [[nodiscard]] virtual SubmitResult send_hvac(const HvacState& state) noexcept = 0;
  [[nodiscard]] virtual SubmitResult send_lamp(const LampState& state) noexcept = 0;
  [[nodiscard]] virtual CommandStatus status(CommandKind kind) const noexcept = 0;

 protected:
  CommandSink() = default;
};

[[nodiscard]] constexpr std::string_view to_string(CommandStatus status) noexcept {
  switch (status) {
    case CommandStatus::Idle:
      return "";
    case CommandStatus::Pending:
      return "SENDING";
    case CommandStatus::Acknowledged:
      return "WAITING FOR STATE";
    case CommandStatus::Confirmed:
      return "";
    case CommandStatus::Rejected:
      return "REFUSED";
    case CommandStatus::TimedOut:
      return "NO CONFIRMATION";
    case CommandStatus::Unavailable:
      return "UNAVAILABLE";
  }
  return "UNAVAILABLE";
}

/// Acknowledge response back from the MCU on command send.
struct Acknowledgement {
  CommandKind kind;
  bool ok;
};

[[nodiscard]] std::optional<Acknowledgement> parse_acknowledgement(std::string_view line) noexcept;

}  // namespace c3::vehicle