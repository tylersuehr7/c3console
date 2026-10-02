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

/// Push-back-only sequence with a compile-time capacity.
///
/// Element access is unchecked, like std::array: callers index below size() and test
/// empty() before front() or back().
template <typename T, std::size_t Capacity>
class FixedVector {
  static_assert(Capacity > 0, "FixedVector needs a non-zero capacity");
  static_assert(std::is_nothrow_copy_assignable_v<T>, "FixedVector elements must copy noexcept");

public:
  /// Maximum number of elements; the container never grows past this.
  static constexpr std::size_t kCapacity = Capacity;

  /// Appends a copy of `value`.
  ///
  /// @param value the element to append
  /// @return true if the element was stored; otherwise false (already full, container unchanged)
  constexpr bool push_back(const T& value) noexcept {
    if (size_ >= Capacity) {
      return false;
    }
    items_[size_++] = value;
    return true;
  }

  /// Removes every element. Storage is not zeroed; only size() changes.
  constexpr void clear() noexcept { size_ = 0; }

  /// @return the number of elements currently held
  [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }

  /// @return true if no elements are held; otherwise false
  [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }

  /// @return true if size() has reached kCapacity; otherwise false
  [[nodiscard]] constexpr bool full() const noexcept { return size_ == Capacity; }

  /// Unchecked element access; `i` must be below size().
  [[nodiscard]] constexpr const T& operator[](std::size_t i) const noexcept { return items_[i]; }
  [[nodiscard]] constexpr T& operator[](std::size_t i) noexcept { return items_[i]; }

  /// Unchecked like operator[]: callers test empty() first.
  [[nodiscard]] constexpr const T& front() const noexcept { return items_[0]; }
  [[nodiscard]] constexpr const T& back() const noexcept { return items_[size_ - 1]; }

  /// Iteration covers exactly the elements pushed so far, not the full capacity.
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
