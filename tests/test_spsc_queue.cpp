#include "utils/spsc_queue.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

struct Sample {
  std::uint32_t seq = 0;
  float value = 0.0F;

  [[nodiscard]] constexpr bool operator==(const Sample&) const noexcept = default;
};

// ---------------------------------------------------------------------------
// Type properties
// ---------------------------------------------------------------------------

TEST_CASE("SpscQueue: push and pop are noexcept", "[spsc_queue]") {
  c3::utils::SpscQueue<int, 4> q;
  STATIC_CHECK(noexcept(q.push(1)));
  STATIC_CHECK(noexcept(q.pop()));
}

// ---------------------------------------------------------------------------
// Empty / single element
// ---------------------------------------------------------------------------

TEST_CASE("pop: returns nullopt on a fresh queue", "[spsc_queue]") {
  c3::utils::SpscQueue<int, 4> q;
  CHECK_FALSE(q.pop().has_value());
  CHECK_FALSE(q.pop().has_value());
}

TEST_CASE("push/pop: a single value round-trips", "[spsc_queue]") {
  c3::utils::SpscQueue<int, 4> q;
  CHECK(q.push(42));
  const auto value = q.pop();
  REQUIRE(value.has_value());
  CHECK(*value == 42);
  CHECK_FALSE(q.pop().has_value());
}

TEST_CASE("push: copies the value rather than aliasing the argument", "[spsc_queue]") {
  c3::utils::SpscQueue<int, 4> q;
  int source = 5;
  CHECK(q.push(source));
  source = 6;
  const auto value = q.pop();
  REQUIRE(value.has_value());
  CHECK(*value == 5);
}

TEST_CASE("push/pop: struct elements round-trip intact", "[spsc_queue]") {
  c3::utils::SpscQueue<Sample, 2> q;
  CHECK(q.push(Sample{.seq = 7, .value = 41.5F}));
  const auto value = q.pop();
  REQUIRE(value.has_value());
  CHECK(*value == Sample{.seq = 7, .value = 41.5F});
}

// ---------------------------------------------------------------------------
// FIFO ordering
// ---------------------------------------------------------------------------

TEST_CASE("push/pop: values come out in insertion order", "[spsc_queue]") {
  c3::utils::SpscQueue<int, 4> q;
  CHECK(q.push(1));
  CHECK(q.push(2));
  CHECK(q.push(3));

  REQUIRE(q.pop() == 1);
  REQUIRE(q.pop() == 2);
  REQUIRE(q.pop() == 3);
  CHECK_FALSE(q.pop().has_value());
}

TEST_CASE("push/pop: interleaved operations keep order", "[spsc_queue]") {
  c3::utils::SpscQueue<int, 3> q;
  CHECK(q.push(1));
  CHECK(q.push(2));
  CHECK(q.pop() == 1);
  CHECK(q.push(3));
  CHECK(q.pop() == 2);
  CHECK(q.push(4));
  CHECK(q.push(5));
  CHECK(q.pop() == 3);
  CHECK(q.pop() == 4);
  CHECK(q.pop() == 5);
  CHECK_FALSE(q.pop().has_value());
}

// ---------------------------------------------------------------------------
// Capacity
// ---------------------------------------------------------------------------

TEST_CASE("push: accepts exactly Capacity values before reporting full", "[spsc_queue]") {
  c3::utils::SpscQueue<int, 3> q;
  CHECK(q.push(1));
  CHECK(q.push(2));
  CHECK(q.push(3));
  CHECK_FALSE(q.push(4));
}

TEST_CASE("push: a full queue rejects and never overwrites unread work", "[spsc_queue]") {
  c3::utils::SpscQueue<int, 3> q;
  CHECK(q.push(1));
  CHECK(q.push(2));
  CHECK(q.push(3));
  CHECK_FALSE(q.push(99));
  CHECK_FALSE(q.push(100));

  CHECK(q.pop() == 1);
  CHECK(q.pop() == 2);
  CHECK(q.pop() == 3);
  CHECK_FALSE(q.pop().has_value());
}

TEST_CASE("push: succeeds again once a slot has been popped", "[spsc_queue]") {
  c3::utils::SpscQueue<int, 2> q;
  CHECK(q.push(1));
  CHECK(q.push(2));
  CHECK_FALSE(q.push(3));

  CHECK(q.pop() == 1);
  CHECK(q.push(3));
  CHECK_FALSE(q.push(4));

  CHECK(q.pop() == 2);
  CHECK(q.pop() == 3);
  CHECK_FALSE(q.pop().has_value());
}

TEST_CASE("push/pop: capacity of one behaves as a single mailbox slot", "[spsc_queue]") {
  c3::utils::SpscQueue<int, 1> q;
  CHECK(q.push(1));
  CHECK_FALSE(q.push(2));
  CHECK(q.pop() == 1);
  CHECK_FALSE(q.pop().has_value());
  CHECK(q.push(2));
  CHECK(q.pop() == 2);
}

TEST_CASE("push/pop: indices wrap around the ring many times without losing order", "[spsc_queue]") {
  c3::utils::SpscQueue<int, 3> q;
  int next_push = 0;
  int next_pop = 0;

  // Keep the queue between one and three deep while cycling through every slot repeatedly.
  for (int round = 0; round < 100; ++round) {
    CHECK(q.push(next_push++));
    CHECK(q.push(next_push++));
    REQUIRE(q.pop() == next_pop++);
    CHECK(q.push(next_push++));
    REQUIRE(q.pop() == next_pop++);
    REQUIRE(q.pop() == next_pop++);
  }
  CHECK_FALSE(q.pop().has_value());
  CHECK(next_push == next_pop);
}

TEST_CASE("push/pop: fill and drain repeatedly across the wrap point", "[spsc_queue]") {
  c3::utils::SpscQueue<int, 4> q;
  int counter = 0;
  for (int round = 0; round < 50; ++round) {
    const int first = counter;
    for (int i = 0; i < 4; ++i) {
      REQUIRE(q.push(counter++));
    }
    CHECK_FALSE(q.push(-1));
    for (int i = 0; i < 4; ++i) {
      REQUIRE(q.pop() == first + i);
    }
    CHECK_FALSE(q.pop().has_value());
  }
}

// ---------------------------------------------------------------------------
// Cross-thread
// ---------------------------------------------------------------------------

TEST_CASE("SpscQueue: one producer and one consumer transfer every value in order", "[spsc_queue]") {
  constexpr std::uint32_t kCount = 200000;
  c3::utils::SpscQueue<Sample, 8> q;

  std::thread producer([&q] {
    for (std::uint32_t i = 0; i < kCount; ++i) {
      const Sample sample{.seq = i, .value = static_cast<float>(i) * 0.5F};
      while (!q.push(sample)) {
        std::this_thread::yield();
      }
    }
  });

  std::vector<Sample> received;
  received.reserve(kCount);
  while (received.size() < kCount) {
    if (const auto sample = q.pop()) {
      received.push_back(*sample);
    } else {
      std::this_thread::yield();
    }
  }
  producer.join();

  REQUIRE(received.size() == kCount);
  bool ordered = true;
  bool intact = true;
  for (std::uint32_t i = 0; i < kCount; ++i) {
    ordered = ordered && received[i].seq == i;
    intact = intact && received[i].value == static_cast<float>(i) * 0.5F;
  }
  CHECK(ordered);
  CHECK(intact);
  CHECK_FALSE(q.pop().has_value());
}

}  // namespace
