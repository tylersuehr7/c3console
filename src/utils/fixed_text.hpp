// Bounded diagnostic text truncates instead of allocating on a failure path.
//
// Built for log lines and fault messages: anything that does not fit is dropped silently,
// because running out of room while reporting a problem must never become a second problem.
#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <concepts>
#include <cstring>
#include <string_view>

namespace c3::utils {

/// Append-only character buffer with a compile-time capacity and no heap.
template <std::size_t Capacity>
class FixedText {
public:
  /// Empties the buffer and zeroes its storage.
  ///
  /// @return the number of characters that were held before the reset
  size_t reset() noexcept {
    const size_t previous_size = size_;
    std::memset(data_.data(), 0, data_.size());
    size_ = 0;
    return previous_size;
  }

  /// @return the number of characters currently held
  [[nodiscard]] size_t size() const noexcept { return size_; }

  /// Appends as much of `text` as fits; the remainder is dropped.
  ///
  /// @param text the characters to append
  void append(std::string_view text) noexcept {
    const auto count = std::min(text.size(), Capacity - size_);
    std::copy_n(text.data(), count, data_.data() + size_);
    size_ += count;
  }

  /// Appends "true" or "false".
  ///
  /// @param value the flag to render
  void append(bool value) noexcept { append(value ? "true" : "false"); }

  /// Appends a C string, or "(null)" when the pointer is null.
  ///
  /// @param text a NUL-terminated string, or nullptr
  void append(const char* text) noexcept { append(std::string_view(text != nullptr ? text : "(null)")); }

  /// Appends a single character.
  ///
  /// @param value the character to append
  void append(char value) noexcept { append(std::string_view(&value, 1)); }

  /// Appends an integer or floating-point number in its shortest round-trip form.
  ///
  /// Uses std::to_chars, so no locale and no allocation. "?" is appended in the unlikely
  /// event the number cannot be rendered.
  ///
  /// @param value the number to render
  template <typename Number>
    requires(std::integral<Number> || std::floating_point<Number>)
  void append(Number value) noexcept {
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (result.ec == std::errc{}) {
      append(std::string_view(buffer.data(), result.ptr - buffer.data()));
    } else {
      append("?");
    }
  }

  /// @return a view of the text held so far; valid until the next append() or reset()
  [[nodiscard]] std::string_view view() const noexcept { return {data_.data(), size_}; }

private:
  std::array<char, Capacity> data_{};
  std::size_t size_ = 0;
};

}  // namespace c3::utils
