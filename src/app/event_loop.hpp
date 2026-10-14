// EventLoop — one thread, one poll(2): wake for readable descriptors (the UART) and for a
// periodic tick (the frame). Descriptors are drained before every tick so a frame never waits
// behind one, and the watchdog is kicked only after a tick completes, so a hung tick stops the
// kicks (spec §5.6). Handlers are registered at init; run() itself does not allocate.
#pragma once

#include "platform/watchdog.hpp"

#include <poll.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <vector>

namespace c3::app {

enum class LoopControl : std::uint8_t { Continue, Stop };

class EventLoop {
public:
  using ReadableHandler = std::function<void()>;
  using TickHandler = std::function<LoopControl()>;

  struct Options {
    std::chrono::microseconds tick_period{16667};  // 60 Hz
    platform::Watchdog* watchdog = nullptr;        // kicked after every completed tick that continues
  };

public:
  explicit EventLoop(Options options);

  /// Wait for `fd` to become readable and call `on_readable`. Error and hang-up conditions
  /// also call it, so the owner can read the failure; an invalid descriptor is dropped from
  /// the wait set after being reported once, so the loop never spins on it.
  void add_readable(int fd, ReadableHandler on_readable);

  /// The periodic handler. Returning LoopControl::Stop ends run(). Without one, run() stops
  /// at the first tick.
  void set_tick(TickHandler on_tick);

  /// Runs until `stop` is set or the tick handler asks to stop. Ticks are paced to the period
  /// without catch-up: after an overrun the next tick is due at once, not several in a row.
  /// Interrupted waits (EINTR, the SIGINT path) are retried; `stop` is noticed within one
  /// period. May be called again after it returns.
  void run(const std::atomic<bool>& stop);

  /// Ticks run so far, including the one that asked to stop.
  [[nodiscard]] std::uint64_t ticks() const noexcept { return ticks_; }

private:
  using Clock = std::chrono::steady_clock;

  struct Registration {
    ReadableHandler on_readable;
  };

  void wait_until(Clock::time_point deadline);
  void dispatch_ready();
  [[nodiscard]] LoopControl tick();

private:
  Options options_;
  std::vector<pollfd> fds_;  // parallel to handlers_; fd < 0 is ignored by poll(2)
  std::vector<Registration> handlers_;
  TickHandler on_tick_;
  std::uint64_t ticks_ = 0;
};

}  // namespace c3::app
