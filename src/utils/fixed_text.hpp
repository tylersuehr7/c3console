// Bounded diagnostic text truncates instead of allocating on a failure path.
#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <concepts>
#include <string_view>

namespace c3::utils {

template <std::size_t Capacity>
class FixedText {
 public:
  size_t reset() noexcept {
    const size_t previous_size = size_;
    std::memset(data_.data(), 0, data_.size());
    size_ = 0;
    return previous_size;
  }

  size_t size() const noexcept { return size_; }

  void append(std::string_view text) noexcept {
    const auto count = std::min(text.size(), Capacity - size_);
    std::copy_n(text.data(), count, data_.data() + size_);
    size_ += count;
  }

  void append(bool value) noexcept { append(value ? "true" : "false"); }

  void append(const char* text) noexcept { append(std::string_view(text != nullptr ? text : "(null)")); }

  void append(char value) noexcept { append(std::string_view(&value, 1)); }

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

  [[nodiscard]] std::string_view view() const noexcept { return {data_.data(), size_}; }

 private:
  std::array<char, Capacity> data_{};
  std::size_t size_ = 0;
};

}  // namespace c3::utils
