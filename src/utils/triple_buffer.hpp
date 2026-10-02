// TripleBuffer — wait-free single-producer / single-consumer snapshot exchange.
//
// The spec asks for sensor state in "a double-buffered snapshot with an atomic index":
// no locks in the render path. Two slots are not enough for that guarantee across threads:
// the producer can wrap around and start overwriting the slot the consumer is still copying.
// Three slots make it safe: at every instant one slot is owned by the producer, one is owned
// by the consumer and one is parked in the middle, and ownership changes hands only through
// a single atomic exchange. Nobody ever reads a slot another side is writing.
//
// This is a mailbox, not a queue: the consumer sees the newest snapshot and skips any it
// did not get to in time. For a queue that preserves every item, use SpscQueue.
//
// T must be trivially copyable: a snapshot is a memcpy, never a shared reference.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace c3::utils {

/// Newest-snapshot mailbox for exactly one producer thread and one consumer thread.
template <typename T>
class TripleBuffer {
  static_assert(std::is_trivially_copyable_v<T>, "snapshots are copied, never shared");

public:
  /// Creates a buffer with nothing fresh; take() returns false until the first publish().
  TripleBuffer() noexcept = default;

  // Not copyable or movable: both threads hold a reference to this exact object, and the
  // slot indices only mean something relative to it.
  TripleBuffer(const TripleBuffer&) = delete;
  TripleBuffer& operator=(const TripleBuffer&) = delete;
  TripleBuffer(TripleBuffer&&) = delete;
  TripleBuffer& operator=(TripleBuffer&&) = delete;
  ~TripleBuffer() = default;

  /// Publishes a snapshot. Producer thread only.
  ///
  /// Copies `value` into the producer's private slot, then swaps that slot into the middle and
  /// marks it fresh. A snapshot the consumer never took is overwritten: the consumer always sees
  /// the newest, never a backlog.
  ///
  /// @param value the snapshot to copy in
  void publish(const T& value) noexcept {
    slots_[back_] = value;
    const std::uint8_t previous = middle_.exchange(back_ | kFresh, std::memory_order_acq_rel);
    back_ = previous & kIndexMask;
  }

  /// Takes the newest unread snapshot. Consumer thread only.
  ///
  /// Each published snapshot is delivered at most once. When nothing new has been published
  /// since the last successful take, `out` is left untouched.
  ///
  /// @param out receives a copy of the snapshot
  /// @return true if a fresh snapshot was copied into `out`; otherwise false
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
  // middle_ packs a slot index in the low two bits and a "fresh" flag in bit 2, so index and
  // flag always change together in one atomic exchange.
  static constexpr std::uint8_t kIndexMask = 0x03;
  static constexpr std::uint8_t kFresh = 0x04;

  std::array<T, 3> slots_{};
  std::uint8_t back_ = 0;                // producer-owned slot; touched only by publish()
  std::uint8_t front_ = 1;               // consumer-owned slot; touched only by take()
  std::atomic<std::uint8_t> middle_{2};  // parked slot, plus kFresh when unread
};

}  // namespace c3::utils
