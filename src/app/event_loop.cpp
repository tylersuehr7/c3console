#include "event_loop.hpp"

#include "logging/log.hpp"

#include <algorithm>
#include <cerrno>
#include <utility>

namespace c3::app {

namespace {
constexpr std::string_view kUnit = "loop";
}

EventLoop::EventLoop(Options options) : options_(options) {}

void EventLoop::add_readable(int fd, ReadableHandler on_readable) {
  fds_.push_back(pollfd{.fd = fd, .events = POLLIN, .revents = 0});
  handlers_.push_back(Registration{.on_readable = std::move(on_readable)});
}

void EventLoop::set_tick(TickHandler on_tick) {
  on_tick_ = std::move(on_tick);
}

void EventLoop::run(const std::atomic<bool>& stop) {
  Clock::time_point next_tick = Clock::now();
  while (!stop.load(std::memory_order_relaxed)) {
    wait_until(next_tick);
    dispatch_ready();

    const Clock::time_point now = Clock::now();

    if (now < next_tick) {
      continue;
    }

    if (tick() == LoopControl::Stop) {
      return;
    }

    // Pace to the period, but an overrun does not schedule catch-up ticks: the next one is
    // due immediately and the loop returns to draining descriptors first.
    next_tick = std::max(next_tick + options_.tick_period, Clock::now());
  }
}

// Blocks in poll(2) until a descriptor is readable or `deadline` passes, whichever is first.
void EventLoop::wait_until(Clock::time_point deadline) {
  const auto remaining = deadline - Clock::now();

  // Round up so a wait never ends before the deadline and the loop spins on `continue`.
  const auto timeout_ms = std::chrono::ceil<std::chrono::milliseconds>(remaining).count();
  const int timeout = static_cast<int>(std::clamp<decltype(timeout_ms)>(timeout_ms, 0, 1000));
  const int ready = ::poll(fds_.data(), static_cast<nfds_t>(fds_.size()), timeout);

  if (ready < 0 && errno != EINTR) {
    logging::error(kUnit, "poll failed: errno ", errno, "; continuing on the tick alone");
  }
}

// Calls the handler of every descriptor poll(2) flagged, then forgets the ones that failed.
void EventLoop::dispatch_ready() {
  for (std::size_t i = 0; i < fds_.size(); ++i) {
    pollfd& p = fds_[i];

    if (p.revents == 0) {
      continue;
    }

    const short events = std::exchange(p.revents, 0);
    if ((events & (POLLNVAL | POLLERR | POLLHUP)) != 0) {
      logging::error(kUnit, "descriptor ", p.fd, " disconnected; no longer waiting on it");
      p.fd = -1;  // poll(2) skips negative descriptors
    }

    // POLLERR / POLLHUP are delivered too: the owner reads, sees the error, and reports it.
    handlers_[i].on_readable();
  }
}

LoopControl EventLoop::tick() {
  const LoopControl control = on_tick_ ? on_tick_() : LoopControl::Stop;
  ++ticks_;

  if (control == LoopControl::Continue && options_.watchdog != nullptr) {
    options_.watchdog->kick();
  }

  return control;
}

}  // namespace c3::app
