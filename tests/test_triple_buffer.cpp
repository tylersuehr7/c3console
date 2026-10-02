#include "utils/triple_buffer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <thread>
#include <type_traits>

namespace {

struct Snapshot {
  std::uint32_t seq = 0;
  float value = 0.0F;

  [[nodiscard]] constexpr bool operator==(const Snapshot&) const noexcept = default;
};

/// Wide enough that a torn copy across threads would be visible as a mismatched word.
struct WideSnapshot {
  std::uint32_t seq = 0;
  std::array<std::uint32_t, 64> words{};

  [[nodiscard]] static WideSnapshot make(std::uint32_t seq) noexcept {
    WideSnapshot s;
    s.seq = seq;
    s.words.fill(seq);
    return s;
  }

  [[nodiscard]] bool consistent() const noexcept {
    for (const auto word : words) {
      if (word != seq) {
        return false;
      }
    }
    return true;
  }
};

// ---------------------------------------------------------------------------
// Type properties
// ---------------------------------------------------------------------------

TEST_CASE("TripleBuffer: is neither copyable nor movable", "[triple_buffer]") {
  STATIC_CHECK_FALSE(std::is_copy_constructible_v<c3::utils::TripleBuffer<int>>);
  STATIC_CHECK_FALSE(std::is_copy_assignable_v<c3::utils::TripleBuffer<int>>);
  STATIC_CHECK_FALSE(std::is_move_constructible_v<c3::utils::TripleBuffer<int>>);
  STATIC_CHECK_FALSE(std::is_move_assignable_v<c3::utils::TripleBuffer<int>>);
}

TEST_CASE("TripleBuffer: publish and take are noexcept", "[triple_buffer]") {
  c3::utils::TripleBuffer<int> buffer;
  int out = 0;
  STATIC_CHECK(noexcept(buffer.publish(1)));
  STATIC_CHECK(noexcept(buffer.take(out)));
}

// ---------------------------------------------------------------------------
// Single-threaded protocol
// ---------------------------------------------------------------------------

TEST_CASE("take: returns false on a fresh buffer and leaves out untouched", "[triple_buffer]") {
  c3::utils::TripleBuffer<int> buffer;
  int out = -1;
  CHECK_FALSE(buffer.take(out));
  CHECK(out == -1);
}

TEST_CASE("publish/take: a published value is taken once", "[triple_buffer]") {
  c3::utils::TripleBuffer<int> buffer;
  buffer.publish(42);

  int out = -1;
  CHECK(buffer.take(out));
  CHECK(out == 42);
}

TEST_CASE("take: a second take without a new publish returns false and leaves out untouched", "[triple_buffer]") {
  c3::utils::TripleBuffer<int> buffer;
  buffer.publish(42);

  int out = -1;
  REQUIRE(buffer.take(out));
  REQUIRE(out == 42);

  out = -1;
  CHECK_FALSE(buffer.take(out));
  CHECK(out == -1);
}

TEST_CASE("publish: several publishes before a take yield only the newest", "[triple_buffer]") {
  c3::utils::TripleBuffer<int> buffer;
  buffer.publish(1);
  buffer.publish(2);
  buffer.publish(3);

  int out = -1;
  CHECK(buffer.take(out));
  CHECK(out == 3);
  CHECK_FALSE(buffer.take(out));
  CHECK(out == 3);
}

TEST_CASE("publish: skipped snapshots are never delivered later", "[triple_buffer]") {
  c3::utils::TripleBuffer<int> buffer;
  buffer.publish(10);
  buffer.publish(20);

  int out = -1;
  REQUIRE(buffer.take(out));
  REQUIRE(out == 20);

  buffer.publish(30);
  CHECK(buffer.take(out));
  CHECK(out == 30);
  CHECK_FALSE(buffer.take(out));
}

TEST_CASE("publish/take: alternating exchanges rotate through all three slots without staleness", "[triple_buffer]") {
  c3::utils::TripleBuffer<int> buffer;
  int out = -1;
  for (int i = 0; i < 1000; ++i) {
    buffer.publish(i);
    REQUIRE(buffer.take(out));
    REQUIRE(out == i);
    REQUIRE_FALSE(buffer.take(out));
  }
}

TEST_CASE("publish/take: bursts of publishes always resolve to the latest value", "[triple_buffer]") {
  c3::utils::TripleBuffer<int> buffer;
  int out = -1;
  int value = 0;
  for (int round = 0; round < 200; ++round) {
    for (int burst = 0; burst < 1 + (round % 5); ++burst) {
      buffer.publish(value++);
    }
    REQUIRE(buffer.take(out));
    REQUIRE(out == value - 1);
    REQUIRE_FALSE(buffer.take(out));
  }
}

TEST_CASE("publish/take: struct snapshots are copied whole", "[triple_buffer]") {
  c3::utils::TripleBuffer<Snapshot> buffer;
  buffer.publish(Snapshot{.seq = 9, .value = 41.5F});

  Snapshot out;
  CHECK(buffer.take(out));
  CHECK(out == Snapshot{.seq = 9, .value = 41.5F});
}

TEST_CASE("publish: later publish does not disturb a snapshot the consumer already holds", "[triple_buffer]") {
  c3::utils::TripleBuffer<Snapshot> buffer;
  buffer.publish(Snapshot{.seq = 1, .value = 1.0F});

  Snapshot held;
  REQUIRE(buffer.take(held));

  buffer.publish(Snapshot{.seq = 2, .value = 2.0F});
  buffer.publish(Snapshot{.seq = 3, .value = 3.0F});
  buffer.publish(Snapshot{.seq = 4, .value = 4.0F});

  CHECK(held == Snapshot{.seq = 1, .value = 1.0F});
}

// ---------------------------------------------------------------------------
// Cross-thread
// ---------------------------------------------------------------------------

TEST_CASE("TripleBuffer: concurrent producer and consumer never observe torn or regressing snapshots",
          "[triple_buffer]") {
  constexpr std::uint32_t kCount = 200000;
  c3::utils::TripleBuffer<WideSnapshot> buffer;

  std::thread producer([&buffer] {
    for (std::uint32_t i = 0; i < kCount; ++i) {
      buffer.publish(WideSnapshot::make(i));
    }
  });

  WideSnapshot out;
  bool any_torn = false;
  bool any_regression = false;
  std::uint32_t takes = 0;
  std::uint32_t last_seq = 0;
  bool have_last = false;

  const auto consume = [&] {
    while (buffer.take(out)) {
      ++takes;
      any_torn = any_torn || !out.consistent();
      any_regression = any_regression || (have_last && out.seq <= last_seq);
      last_seq = out.seq;
      have_last = true;
    }
  };

  // Keep draining until the producer is done, then drain once more for the final snapshot.
  while (!have_last || last_seq != kCount - 1) {
    consume();
    if (!have_last || last_seq != kCount - 1) {
      std::this_thread::yield();
    }
  }
  producer.join();
  consume();

  CHECK_FALSE(any_torn);
  CHECK_FALSE(any_regression);
  CHECK(have_last);
  CHECK(last_seq == kCount - 1);
  CHECK(takes >= 1);
  CHECK(takes <= kCount);
}

}  // namespace
