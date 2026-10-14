// event_loop.cpp — poll(2) on descriptors plus a paced tick, with the watchdog kicked per tick.
#include "app/event_loop.hpp"
#include "logging/log.hpp"
#include "platform/watchdog.hpp"

#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string_view>
#include <thread>

using c3::app::EventLoop;
using c3::app::LoopControl;
using namespace std::chrono_literals;

namespace {

using Clock = std::chrono::steady_clock;

class CountingWatchdog final : public c3::platform::Watchdog {
public:
  int kicks = 0;
  void kick() noexcept override { ++kicks; }
};

/// Discards log lines (the loop reports disconnected descriptors).
class QuietLog {
public:
  QuietLog() : sink_(c3::logging::Sink{.context = nullptr, .emit = &QuietLog::discard}) {}

private:
  static void discard(void*, std::string_view) noexcept {}
  c3::logging::ScopedSink sink_;
};

struct Pipe {
  int read_end = -1;
  int write_end = -1;
  Pipe() {
    std::array<int, 2> fds{};
    REQUIRE(::pipe(fds.data()) == 0);
    read_end = fds[0];
    write_end = fds[1];
  }
  ~Pipe() {
    close_write();
    static_cast<void>(::close(read_end));
  }
  Pipe(const Pipe&) = delete;
  Pipe& operator=(const Pipe&) = delete;
  Pipe(Pipe&&) = delete;
  Pipe& operator=(Pipe&&) = delete;

  void write_byte() const { REQUIRE(::write(write_end, "x", 1) == 1); }
  void close_write() {
    if (write_end >= 0) {
      static_cast<void>(::close(write_end));
      write_end = -1;
    }
  }
  /// Reads whatever is pending so the descriptor goes quiet again.
  [[nodiscard]] ssize_t drain() const {
    std::array<char, 64> buf{};
    return ::read(read_end, buf.data(), buf.size());
  }
};

[[nodiscard]] EventLoop::Options fast(std::chrono::microseconds period = 2ms,
                                      c3::platform::Watchdog* watchdog = nullptr) {
  return {.tick_period = period, .watchdog = watchdog};
}

const std::atomic<bool> kNeverStop{false};

// ---------------------------------------------------------------------------
// ticks
// ---------------------------------------------------------------------------

TEST_CASE("EventLoop: without a tick handler the first tick stops the loop", "[event_loop]") {
  EventLoop loop(fast());
  loop.run(kNeverStop);
  CHECK(loop.ticks() == 1);
}

TEST_CASE("EventLoop: the tick handler runs until it asks to stop", "[event_loop]") {
  EventLoop loop(fast());
  int calls = 0;
  loop.set_tick([&] { return ++calls < 5 ? LoopControl::Continue : LoopControl::Stop; });
  loop.run(kNeverStop);
  CHECK(calls == 5);
  CHECK(loop.ticks() == 5);
}

TEST_CASE("EventLoop: the watchdog is kicked after every tick that continues, not the one that stops", "[event_loop]") {
  CountingWatchdog dog;
  EventLoop loop(fast(2ms, &dog));
  int calls = 0;
  loop.set_tick([&] { return ++calls < 4 ? LoopControl::Continue : LoopControl::Stop; });
  loop.run(kNeverStop);
  CHECK(loop.ticks() == 4);
  CHECK(dog.kicks == 3);
}

TEST_CASE("EventLoop: a stop flag already set returns before any tick", "[event_loop]") {
  EventLoop loop(fast());
  const std::atomic<bool> stop{true};
  int calls = 0;
  loop.set_tick([&] {
    ++calls;
    return LoopControl::Continue;
  });
  loop.run(stop);
  CHECK(calls == 0);
  CHECK(loop.ticks() == 0);
}

TEST_CASE("EventLoop: a stop flag raised from another thread ends the loop within a period", "[event_loop]") {
  EventLoop loop(fast(5ms));
  std::atomic<bool> stop{false};
  loop.set_tick([] { return LoopControl::Continue; });
  std::thread stopper([&] {
    std::this_thread::sleep_for(50ms);
    stop.store(true);
  });
  const auto started = Clock::now();
  loop.run(stop);
  stopper.join();
  const auto elapsed = Clock::now() - started;
  CHECK(elapsed >= 45ms);
  CHECK(elapsed < 1s);
  CHECK(loop.ticks() >= 3);
}

TEST_CASE("EventLoop: ticks are paced to the period", "[event_loop]") {
  EventLoop loop(fast(20ms));
  int calls = 0;
  loop.set_tick([&] { return ++calls < 6 ? LoopControl::Continue : LoopControl::Stop; });
  const auto started = Clock::now();
  loop.run(kNeverStop);
  const auto elapsed = Clock::now() - started;
  CHECK(elapsed >= 100ms);  // five intervals between six ticks
  CHECK(elapsed < 1s);
}

TEST_CASE("EventLoop: an overrunning tick is not followed by catch-up ticks", "[event_loop]") {
  EventLoop loop(fast(5ms));
  int calls = 0;
  std::array<Clock::time_point, 4> starts{};
  loop.set_tick([&] {
    starts[static_cast<std::size_t>(calls)] = Clock::now();
    std::this_thread::sleep_for(30ms);  // six periods
    return ++calls < 4 ? LoopControl::Continue : LoopControl::Stop;
  });
  loop.run(kNeverStop);
  CHECK(loop.ticks() == 4);
  for (std::size_t i = 1; i < starts.size(); ++i) {
    // each tick starts as soon as the previous one ends: no backlog, no idle period added
    CHECK(starts[i] - starts[i - 1] >= 30ms);
    CHECK(starts[i] - starts[i - 1] < 300ms);
  }
}

TEST_CASE("EventLoop: run() can be called again after it returns", "[event_loop]") {
  EventLoop loop(fast());
  int calls = 0;
  loop.set_tick([&] { return ++calls % 3 == 0 ? LoopControl::Stop : LoopControl::Continue; });
  loop.run(kNeverStop);
  CHECK(loop.ticks() == 3);
  loop.run(kNeverStop);
  CHECK(loop.ticks() == 6);
  CHECK(calls == 6);
}

// ---------------------------------------------------------------------------
// descriptors
// ---------------------------------------------------------------------------

TEST_CASE("EventLoop: a readable descriptor is dispatched before the next tick", "[event_loop]") {
  Pipe pipe;
  EventLoop loop(fast(50ms));
  int readable = 0;
  int readable_at_tick = -1;
  loop.add_readable(pipe.read_end, [&] {
    ++readable;
    CHECK(pipe.drain() == 1);
  });
  loop.set_tick([&] {
    readable_at_tick = readable;
    return LoopControl::Stop;
  });
  pipe.write_byte();
  loop.run(kNeverStop);
  CHECK(readable == 1);
  CHECK(readable_at_tick == 1);
}

TEST_CASE("EventLoop: data arriving mid-wait wakes the loop without waiting for the tick", "[event_loop]") {
  Pipe pipe;
  EventLoop loop(fast(500ms));
  Clock::time_point woke;
  loop.add_readable(pipe.read_end, [&] {
    woke = Clock::now();
    static_cast<void>(pipe.drain());
  });
  int calls = 0;
  loop.set_tick([&] { return ++calls < 2 ? LoopControl::Continue : LoopControl::Stop; });
  std::thread writer([&] {
    std::this_thread::sleep_for(20ms);
    pipe.write_byte();
  });
  const auto started = Clock::now();
  loop.run(kNeverStop);
  writer.join();
  CHECK(woke - started >= 15ms);
  CHECK(woke - started < 400ms);  // well before the second tick at 500 ms
}

TEST_CASE("EventLoop: several descriptors are dispatched independently", "[event_loop]") {
  Pipe a;
  Pipe b;
  EventLoop loop(fast(50ms));
  int from_a = 0;
  int from_b = 0;
  loop.add_readable(a.read_end, [&] {
    ++from_a;
    static_cast<void>(a.drain());
  });
  loop.add_readable(b.read_end, [&] {
    ++from_b;
    static_cast<void>(b.drain());
  });
  loop.set_tick([] { return LoopControl::Stop; });
  b.write_byte();
  loop.run(kNeverStop);
  CHECK(from_a == 0);
  CHECK(from_b == 1);
}

TEST_CASE("EventLoop: a hung-up descriptor is reported once and then no longer waited on", "[event_loop]") {
  const QuietLog quiet;
  Pipe pipe;
  EventLoop loop(fast(2ms));
  int readable = 0;
  ssize_t last_read = -1;
  loop.add_readable(pipe.read_end, [&] {
    ++readable;
    last_read = pipe.drain();  // 0: end of file, the owner learns why
  });
  int calls = 0;
  loop.set_tick([&] { return ++calls < 20 ? LoopControl::Continue : LoopControl::Stop; });
  pipe.close_write();
  loop.run(kNeverStop);
  CHECK(readable == 1);  // not once per tick: the descriptor was dropped from the set
  CHECK(last_read == 0);
  CHECK(loop.ticks() == 20);
}

TEST_CASE("EventLoop: an invalid descriptor is reported once and never spun on", "[event_loop]") {
  const QuietLog quiet;
  int closed = -1;
  {
    const Pipe scratch;
    closed = ::dup(scratch.read_end);
  }
  static_cast<void>(::close(closed));  // now a number that is not open
  EventLoop loop(fast(2ms));
  int readable = 0;
  loop.add_readable(closed, [&] { ++readable; });
  int calls = 0;
  loop.set_tick([&] { return ++calls < 10 ? LoopControl::Continue : LoopControl::Stop; });
  const auto started = Clock::now();
  loop.run(kNeverStop);
  CHECK(readable == 1);
  CHECK(loop.ticks() == 10);
  CHECK(Clock::now() - started >= 15ms);  // the ticks still paced; poll did not return instantly forever
}

TEST_CASE("EventLoop: Options default to 60 Hz with no watchdog", "[event_loop]") {
  const EventLoop::Options o{};
  CHECK(o.tick_period == std::chrono::microseconds{16667});
  CHECK(o.watchdog == nullptr);
}

}  // namespace
