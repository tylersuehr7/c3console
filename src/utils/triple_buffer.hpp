// TripleBuffer — wait-free single-producer / single-consumer snapshot exchange.
//
// The spec asks for sensor state in "a double-buffered snapshot with an atomic index":
// no locks in the render path. Two slots are not enough for that guarantee across threads:
// the producer can wrap around and start overwriting the slot the consumer is still copying.
// Three slots make it safe: at every instant one slot is owned by the producer, one is owned
// by the consumer and one is parked in the middle, and ownership changes hands only through
// a single atomic exchange. Nobody ever reads a slot another side is writing.
//
// T must be trivially copyable: a snapshot is a memcpy, never a shared reference.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace c3::utils {

template <typename T>
class TripleBuffer {
  static_assert(std::is_trivially_copyable_v<T>, "snapshots are copied, never shared");

 public:
  TripleBuffer() noexcept = default;

  TripleBuffer(const TripleBuffer&) = delete;
  TripleBuffer& operator=(const TripleBuffer&) = delete;
  TripleBuffer(TripleBuffer&&) = delete;
  TripleBuffer& operator=(TripleBuffer&&) = delete;
  ~TripleBuffer() = default;

  /// Producer side. Writes `value` into the producer's private slot, then swaps it into the
  /// middle and marks it fresh. If the consumer had not taken the previous value it is
  /// overwritten: the consumer always sees the newest snapshot, never a queue.
  void publish(const T& value) noexcept {
    slots_[back_] = value;
    const std::uint8_t previous = middle_.exchange(back_ | kFresh, std::memory_order_acq_rel);
    back_ = previous & kIndexMask;
  }

  /// Consumer side. Returns true and copies the newest snapshot into `out` when a value has
  /// been published since the last call; otherwise leaves `out` alone and returns false.
  [[nodiscard]] bool take(T& out) noexcept {
    if ((middle_.load(std::memory_order_acquire) & kFresh) == 0) {
      return false;
    }
    const std::uint8_t previous = middle_.exchange(front_, std::memory_order_acq_rel);
    front_ = previous & kIndexMask;
    out = slots_[front_];
    return true;
  }

 private:
  static constexpr std::uint8_t kIndexMask = 0x03;
  static constexpr std::uint8_t kFresh = 0x04;

  std::array<T, 3> slots_{};
  std::uint8_t back_ = 0;                // producer-owned slot
  std::uint8_t front_ = 1;               // consumer-owned slot
  std::atomic<std::uint8_t> middle_{2};  // parked slot, plus kFresh when unread
};

}  // namespace c3::utils
