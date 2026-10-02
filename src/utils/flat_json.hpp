// Fixed-buffer protocol objects reject escapes; all current command keys are ASCII literals.
#pragma once

#include <cstddef>
#include <string_view>

namespace c3::utils {

// One member of a flat object: the key and the raw value token (quotes stripped for strings).
struct JsonMember {
  std::string_view key;
  std::string_view value;
  bool is_string = false;
};

// Minimal scanner for `{"k":v,...}` where v is a string, number, true or false.
class FlatJsonObject {
 public:
  explicit constexpr FlatJsonObject(std::string_view text) noexcept : text_(text) {}

  /// Reads the next member; false at the closing brace or on malformed input (see failed()).
  [[nodiscard]] bool next(JsonMember& out) noexcept {
    skip_ws();
    if (!opened_) {
      if (!take('{')) {
        return fail();
      }
      opened_ = true;
      skip_ws();
      if (take('}')) {
        closed_ = true;
        return false;
      }
    } else {
      if (take('}')) {
        closed_ = true;
        return false;
      }
      if (!take(',')) {
        return fail();
      }
      skip_ws();
    }
    if (!string(out.key)) {
      return fail();
    }
    skip_ws();
    if (!take(':')) {
      return fail();
    }
    skip_ws();
    if (peek() == '"') {
      out.is_string = true;
      return string(out.value) || fail();
    }
    out.is_string = false;
    const std::size_t start = pos_;
    while (pos_ < text_.size() && text_[pos_] != ',' && text_[pos_] != '}' && text_[pos_] != ' ') {
      ++pos_;
    }
    out.value = text_.substr(start, pos_ - start);
    return !out.value.empty() || fail();
  }

  /// True when the object closed cleanly with nothing but whitespace after it.
  [[nodiscard]] bool complete() noexcept {
    skip_ws();
    return closed_ && !failed_ && pos_ >= text_.size();
  }

  [[nodiscard]] constexpr bool failed() const noexcept { return failed_; }

 private:
  constexpr void skip_ws() noexcept {
    while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\r')) {
      ++pos_;
    }
  }

  [[nodiscard]] constexpr char peek() const noexcept { return pos_ < text_.size() ? text_[pos_] : '\0'; }

  constexpr bool take(char c) noexcept {
    if (peek() != c) {
      return false;
    }
    ++pos_;
    return true;
  }

  constexpr bool string(std::string_view& out) noexcept {
    if (!take('"')) {
      return false;
    }
    const std::size_t start = pos_;
    while (pos_ < text_.size() && text_[pos_] != '"') {
      if (text_[pos_] == '\\') {
        return false;  // escapes never appear in commands; refuse rather than guess
      }
      ++pos_;
    }
    if (pos_ >= text_.size()) {
      return false;
    }
    out = text_.substr(start, pos_ - start);
    ++pos_;
    return true;
  }

  constexpr bool fail() noexcept {
    failed_ = true;
    return false;
  }

  std::string_view text_;
  std::size_t pos_ = 0;
  bool opened_ = false;
  bool closed_ = false;
  bool failed_ = false;
};

}  // namespace c3::utils
