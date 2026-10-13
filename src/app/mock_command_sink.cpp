#include "mock_command_sink.hpp"

#include "logging/log.hpp"

#include <array>

namespace c3::app {
namespace {

// Logs the command line exactly as UartSource would transmit it (without the newline).
void log_command(std::string_view unit, std::size_t n,
                 const std::array<char, vehicle::kMaxCommandLength>& line) noexcept {
  if (n == 0) {
    logging::error(unit, "command did not fit the line buffer");
    return;
  }
  logging::info(unit, "\xE2\x86\x92 MCU ", std::string_view(line.data(), n));
}

}  // namespace

vehicle::SubmitResult MockCommandSink::send_hvac(const vehicle::HvacState& s) noexcept {
  if (mock_ == nullptr) {
    return vehicle::SubmitResult::Unavailable;
  }

  if (!vehicle::is_valid(s)) {
    return vehicle::SubmitResult::Invalid;
  }

  std::array<char, vehicle::kMaxCommandLength> line{};
  log_command("hvac", vehicle::format_hvac_command(s, line), line);
  loads_.blower = s.blower;
  loads_.ac = s.ac_enabled;
  loads_.mode = s.mode;
  mock_->set_loads(loads_);

  return vehicle::SubmitResult::Accepted;
}

vehicle::SubmitResult MockCommandSink::send_lamp(const vehicle::LampState& s) noexcept {
  if (mock_ == nullptr) {
    return vehicle::SubmitResult::Unavailable;
  }

  if (!vehicle::is_valid(s)) {
    return vehicle::SubmitResult::Invalid;  // the same gate UartSource applies
  }

  std::array<char, vehicle::kMaxCommandLength> line{};
  log_command("lamp", vehicle::format_lamp_command(s, line), line);
  loads_.headlights = s.headlights_on;
  loads_.doors = s.lamp_doors_open;
  loads_.high = s.highbeams_on;
  mock_->set_loads(loads_);

  return vehicle::SubmitResult::Accepted;
}

}  // namespace c3::app
