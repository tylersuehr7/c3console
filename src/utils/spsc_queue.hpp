// One producer and one consumer own opposite ends; a full queue never overwrites unread work.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <type_traits>

namespace c3::utils {

template <typename T, std::size_t Capacity>
class SpscQueue {
  static_assert(Capacity > 0 && std::is_trivially_copyable_v<T>);
  static_assert(std::atomic<std::size_t>::is_always_lock_free);

 public:
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
  std::array<T, Capacity + 1> values_{};
  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};
};

}  // namespace c3::utils
