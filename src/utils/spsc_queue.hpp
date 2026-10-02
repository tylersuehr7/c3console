// Bounded single-producer / single-consumer ring buffer.
//
// Exactly one thread calls push() and exactly one calls pop(); those two may run concurrently
// without locks. A full queue rejects new work rather than overwriting unread entries, so the
// producer always learns about back-pressure. One slot is kept empty to tell full from empty,
// which is why storage is Capacity + 1.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <type_traits>

namespace c3::utils {

/// Lock-free FIFO for exactly one producer thread and one consumer thread.
template <typename T, std::size_t Capacity>
class SpscQueue {
  // Elements are copied in and out by value; a non-trivial copy could throw or allocate.
  static_assert(Capacity > 0 && std::is_trivially_copyable_v<T>);
  // A locking atomic would make the hot path block, defeating the point of the queue.
  static_assert(std::atomic<std::size_t>::is_always_lock_free);

public:
  /// Appends a value at the tail. Producer thread only.
  ///
  /// @param value the item to copy into the queue
  /// @return true if the item was queued; otherwise false (queue already holds Capacity items)
  [[nodiscard]] bool push(const T& value) noexcept {
    const auto tail = tail_.load(std::memory_order_relaxed);
    const auto next = (tail + 1) % (Capacity + 1);
    if (next == head_.load(std::memory_order_acquire)) {
      return false;
    }
    values_[tail] = value;
    tail_.store(next, std::memory_order_release);
    return true;
  }

  /// Removes the oldest item. Consumer thread only.
  ///
  /// @return the item if one was queued; otherwise nullopt
  [[nodiscard]] std::optional<T> pop() noexcept {
    const auto head = head_.load(std::memory_order_relaxed);
    if (head == tail_.load(std::memory_order_acquire)) {
      return std::nullopt;
    }
    const T value = values_[head];
    head_.store((head + 1) % (Capacity + 1), std::memory_order_release);
    return value;
  }

private:
  std::array<T, Capacity + 1> values_{};  // ring storage; one slot always empty
  std::atomic<std::size_t> head_{0};      // next slot to pop; written by the consumer only
  std::atomic<std::size_t> tail_{0};      // next slot to fill; written by the producer only
};

}  // namespace c3::utils
