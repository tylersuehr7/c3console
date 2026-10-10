// UartSource — frames from the MCU over a serial line (spec §4: 115200 8N1, 10 Hz).
//
// Non-blocking reads feed a LineReader; each complete line goes through parse_frame.
// Bad CRCs and malformed lines are counted in health() and dropped, never rendered
// (acceptance: "CRC failures are counted and dropped, never parsed"). The link is Live while
// good frames arrive within the stall timeout, Stalled after, Failed if the device goes away.
// poll() never allocates or throws. The descriptor is exposed for the event loop.
#pragma once

#include "frame.hpp"
#include "protocol.hpp"
#include "sensor_source.hpp"
#include "utils/clock.hpp"
#include "utils/unique_fd.hpp"
#include "vehicle/vehicle_commands.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace c3::telemetry {

class UartSource final : public SensorSource, public vehicle::CommandSink {
public:
  struct Options {
    std::string device = "/dev/ttyAMA0";
    int baud = 115200;
    std::uint32_t stall_timeout_ms = 1000;
  };

  struct Result {
    std::unique_ptr<UartSource> source;  // null when `error` is set
    std::string error;
  };

  /// Opens and configures the device (raw, 8N1, non-blocking). Init-time.
  [[nodiscard]] static Result open(const Options& options, utils::MonotonicClock clock);

  /// Adopts an already-open, non-blocking descriptor without touching termios: bench rigs,
  /// pseudo-terminals and tests.
  [[nodiscard]] static std::unique_ptr<UartSource> adopt(utils::UniqueFd fd, utils::MonotonicClock clock,
                                                         std::uint32_t stall_timeout_ms);

  [[nodiscard]] std::optional<Frame> poll() noexcept override;
  [[nodiscard]] SourceHealth health() const noexcept override;

  [[nodiscard]] vehicle::SubmitResult send_hvac(const vehicle::HvacState& state) noexcept override;
  [[nodiscard]] vehicle::SubmitResult send_lamp(const vehicle::LampState& state) noexcept override;
  [[nodiscard]] vehicle::CommandStatus status(vehicle::CommandKind kind) const noexcept override;
  [[nodiscard]] int fd() const noexcept { return fd_.get(); }
  [[nodiscard]] int descriptor() const noexcept override { return fd_.get(); }
  /// Lines dropped for exceeding kMaxLineLength (garbage bursts).
  [[nodiscard]] std::uint32_t lines_dropped() const noexcept { return reader_.dropped(); }
  /// Reason of the most recent rejected line, for the bench log.
  [[nodiscard]] ParseError last_error() const noexcept { return last_error_; }

private:
  UartSource(utils::UniqueFd fd, utils::MonotonicClock clock, std::uint32_t stall_timeout_ms) noexcept;
  void on_line(std::string_view line, std::uint64_t now_ms) noexcept;

  [[nodiscard]] vehicle::SubmitResult submit(vehicle::CommandKind kind, std::size_t length) noexcept;
  void service_command() noexcept;
  void confirm_command(const Frame& frame) noexcept;
  std::array<char, vehicle::kMaxCommandLength + 1> transmit_{};
  std::size_t transmit_size_ = 0;
  std::size_t transmit_pos_ = 0;
  std::optional<vehicle::CommandKind> pending_command_;
  std::array<vehicle::CommandStatus, 2> command_status_{};
  vehicle::HvacState requested_hvac_;
  vehicle::LampState requested_lamps_;
  std::uint64_t command_started_ms_ = 0;
  bool command_quarantined_ = false;
  utils::UniqueFd fd_;
  utils::MonotonicClock clock_;
  std::uint32_t stall_timeout_ms_;
  LineReader reader_;
  std::array<char, 256> chunk_{};
  std::optional<Frame> latest_;
  std::size_t chunk_pos_ = 0;
  std::size_t chunk_size_ = 0;
  bool failed_ = false;
  ParseError last_error_ = ParseError::None;
  SourceHealth health_{};
};

}  // namespace c3::telemetry
