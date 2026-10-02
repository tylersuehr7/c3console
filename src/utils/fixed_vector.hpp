// A fixed-capacity sequence for hot-path containers.
//
// No heap, no exceptions, trivially copyable when T is. Used wherever the render
// loop or the telemetry path needs "a few of something" without allocating.
#pragma once

#include <array>
#include <cstddef>
#include <iterator>
#include <type_traits>

namespace c3::utils {

/// operator[] is unchecked, like std::array: callers index below size().
template <typename T, std::size_t Capacity>
class FixedVector {
  static_assert(Capacity > 0, "FixedVector needs a non-zero capacity");
  static_assert(std::is_nothrow_copy_assignable_v<T>, "FixedVector elements must copy noexcept");

 public:
  static constexpr std::size_t kCapacity = Capacity;

  /// Appends `value`; returns false and leaves the container unchanged when full.
  constexpr bool push_back(const T& value) noexcept {
    if (size_ >= Capacity) {
      return false;
    }
    items_[size_++] = value;
    return true;
  }

  constexpr void clear() noexcept { size_ = 0; }

  [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] constexpr bool full() const noexcept { return size_ == Capacity; }

  [[nodiscard]] constexpr const T& operator[](std::size_t i) const noexcept { return items_[i]; }
  [[nodiscard]] constexpr T& operator[](std::size_t i) noexcept { return items_[i]; }

  /// Unchecked like operator[]: callers test empty() first.
  [[nodiscard]] constexpr const T& front() const noexcept { return items_[0]; }
  [[nodiscard]] constexpr const T& back() const noexcept { return items_[size_ - 1]; }

  [[nodiscard]] constexpr const T* begin() const noexcept { return items_.data(); }
  [[nodiscard]] constexpr const T* end() const noexcept {
    return std::next(items_.data(), static_cast<std::ptrdiff_t>(size_));
  }
  [[nodiscard]] constexpr T* begin() noexcept { return items_.data(); }
  [[nodiscard]] constexpr T* end() noexcept { return std::next(items_.data(), static_cast<std::ptrdiff_t>(size_)); }

 private:
  std::array<T, Capacity> items_{};
  std::size_t size_ = 0;
};

}  // namespace c3::utils
