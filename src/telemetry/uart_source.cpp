#include "uart_source.hpp"

#include "freshness.hpp"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <span>
#include <string>
#include <system_error>
#include <utility>

namespace c3::telemetry {

namespace {

[[nodiscard]] std::string errno_text() {
  return std::system_category().message(errno);
}

[[nodiscard]] std::optional<speed_t> baud_constant(int baud) noexcept {
  switch (baud) {
    case 9600:
      return B9600;
    case 19200:
      return B19200;
    case 38400:
      return B38400;
    case 57600:
      return B57600;
    case 115200:
      return B115200;
    case 230400:
      return B230400;
    default:
      return std::nullopt;
  }
}

}  // namespace

UartSource::UartSource(utils::UniqueFd fd, utils::MonotonicClock clock, std::uint32_t stall_timeout_ms) noexcept
    : fd_(std::move(fd)), clock_(clock), stall_timeout_ms_(stall_timeout_ms) {}

UartSource::Result UartSource::open(const Options& options, utils::MonotonicClock clock) {
  const auto speed = baud_constant(options.baud);
  if (!speed) {
    return {nullptr, "unsupported baud rate " + std::to_string(options.baud)};
  }

  utils::UniqueFd fd(::open(options.device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC));
  if (!fd) {
    return {nullptr, options.device + ": " + errno_text()};
  }

  termios tio{};
  if (tcgetattr(fd.get(), &tio) != 0) {
    return {nullptr, options.device + ": tcgetattr: " + errno_text()};
  }

  cfmakeraw(&tio);  // 8 bits, no parity, no flow control, no line editing
  tio.c_cflag |= CLOCAL | CREAD;
  tio.c_cc[VMIN] = 1;  // with O_NONBLOCK: EAGAIN when idle, bytes as they arrive
  tio.c_cc[VTIME] = 0;

  if (cfsetispeed(&tio, *speed) != 0 || cfsetospeed(&tio, *speed) != 0 || tcsetattr(fd.get(), TCSANOW, &tio) != 0) {
    return {nullptr, options.device + ": tcsetattr: " + errno_text()};
  }

  static_cast<void>(tcflush(fd.get(), TCIFLUSH));  // start on a line boundary, not mid-frame
  return {std::unique_ptr<UartSource>(new UartSource(std::move(fd), clock, options.stall_timeout_ms)), {}};
}

std::unique_ptr<UartSource> UartSource::adopt(utils::UniqueFd fd, utils::MonotonicClock clock,
                                              std::uint32_t stall_timeout_ms) {
  return std::unique_ptr<UartSource>(new UartSource(std::move(fd), clock, stall_timeout_ms));
}

void UartSource::on_line(std::string_view line, std::uint64_t now_ms) noexcept {
  if (const auto ack = vehicle::parse_acknowledgement(line)) {
    if (pending_command_ == ack->kind && transmit_pos_ == transmit_size_) {
      command_status_[static_cast<std::size_t>(ack->kind)] =
          ack->ok ? vehicle::CommandStatus::Acknowledged : vehicle::CommandStatus::Rejected;
      if (!ack->ok) {
        pending_command_.reset();
      }
    }
    return;
  }

  const ParseResult r = parse_frame(line);
  switch (r.error) {
    case ParseError::None:
      latest_ = r.frame;
      confirm_command(*r.frame);
      ++health_.frames_ok;
      health_.last_frame_monotonic_ms = now_ms;
      return;
    case ParseError::Empty:
      return;  // blank lines between frames are not an error
    case ParseError::CrcMismatch:
      ++health_.frames_bad_crc;
      break;
    case ParseError::TooLong:
    case ParseError::MissingCrc:
    case ParseError::BadCrcField:
    case ParseError::Malformed:
    case ParseError::MissingField:
    case ParseError::OutOfRange:
    case ParseError::UnknownFault:
    case ParseError::TooManyFaults:
      ++health_.frames_malformed;
      break;
  }

  last_error_ = r.error;
}

std::optional<Frame> UartSource::poll() noexcept {
  const std::uint64_t now = clock_();
  service_command();
  latest_.reset();

  constexpr std::size_t kByteBudget = 1024;
  for (std::size_t consumed = 0; consumed < kByteBudget && !failed_; ++consumed) {
    if (chunk_pos_ == chunk_size_) {
      const ssize_t count = ::read(fd_.get(), chunk_.data(), chunk_.size());
      if (count <= 0) {
        if (count == 0 || (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
          failed_ = true;
        }
        break;
      }
      chunk_pos_ = 0;
      chunk_size_ = static_cast<std::size_t>(count);
    }

    reader_.feed(std::span<const char>(&chunk_[chunk_pos_++], 1),
                 [this, now](std::string_view line) noexcept { on_line(line, now); });

    // Leave remaining bytes buffered so every accepted frame reaches the recorder in order.
    if (latest_) {
      break;
    }
  }

  return latest_;
}

SourceHealth UartSource::health() const noexcept {
  SourceHealth result = health_;
  result.state = link_state(failed_, result.frames_ok > 0, clock_(), result.last_frame_monotonic_ms, stall_timeout_ms_);
  return result;
}

vehicle::SubmitResult UartSource::send_hvac(const vehicle::HvacState& state) noexcept {
  if (pending_command_) {
    return vehicle::SubmitResult::Busy;
  }

  if (!vehicle::is_valid(state)) {
    return vehicle::SubmitResult::Invalid;
  }

  requested_hvac_ = state;
  return submit(vehicle::CommandKind::Hvac, vehicle::format_hvac_command(state, transmit_));
}

vehicle::SubmitResult UartSource::send_lamp(const vehicle::LampState& state) noexcept {
  if (pending_command_) {
    return vehicle::SubmitResult::Busy;
  }

  if (!vehicle::is_valid(state)) {
    return vehicle::SubmitResult::Invalid;
  }

  requested_lamps_ = state;
  return submit(vehicle::CommandKind::Lamp, vehicle::format_lamp_command(state, transmit_));
}

vehicle::SubmitResult UartSource::submit(vehicle::CommandKind kind, std::size_t length) noexcept {
  if (command_quarantined_ || health().state != LinkState::Live) {
    return vehicle::SubmitResult::Unavailable;
  }

  if (length == 0 || length >= transmit_.size()) {
    return vehicle::SubmitResult::Invalid;
  }

  transmit_[length] = '\n';
  transmit_size_ = length + 1;
  transmit_pos_ = 0;
  pending_command_ = kind;
  command_started_ms_ = clock_();
  command_status_[static_cast<std::size_t>(kind)] = vehicle::CommandStatus::Pending;

  return vehicle::SubmitResult::Accepted;
}

void UartSource::service_command() noexcept {
  if (!pending_command_) {
    return;
  }

  auto& state = command_status_[static_cast<std::size_t>(*pending_command_)];
  if (failed_ || frame_age(clock_(), command_started_ms_) >= kMaxStallTimeoutMs) {
    state = failed_ ? vehicle::CommandStatus::Unavailable : vehicle::CommandStatus::TimedOut;
    // The legacy protocol has no transaction IDs. A late ack must never confirm a later request.
    command_quarantined_ = true;
    pending_command_.reset();
    return;
  }

  if (transmit_pos_ < transmit_size_) {
    const auto count = ::write(fd_.get(), transmit_.data() + transmit_pos_, transmit_size_ - transmit_pos_);
    if (count > 0) {
      transmit_pos_ += static_cast<std::size_t>(count);
    } else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      failed_ = true;
    }
  }
}

void UartSource::confirm_command(const Frame& frame) noexcept {
  if (!pending_command_ || !frame.relays) {
    return;
  }

  auto& status = command_status_[static_cast<std::size_t>(*pending_command_)];
  if (status != vehicle::CommandStatus::Acknowledged) {
    return;
  }

  const auto& relays = *frame.relays;

  const bool matches =
      *pending_command_ == vehicle::CommandKind::Hvac
          ? vehicle::HvacState{.mode = relays.mode, .blower = relays.blower, .ac_enabled = relays.ac_enabled} ==
                requested_hvac_
          : relays.headlights_on == requested_lamps_.headlights_on &&
                relays.lamp_doors_open == requested_lamps_.lamp_doors_open;

  if (matches) {
    status = vehicle::CommandStatus::Confirmed;
    pending_command_.reset();
  }
}

vehicle::CommandStatus UartSource::status(vehicle::CommandKind kind) const noexcept {
  return command_status_[static_cast<std::size_t>(kind)];
}

}  // namespace c3::telemetry
