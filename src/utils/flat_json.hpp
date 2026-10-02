// Fixed-buffer scanner for one-level JSON objects on the MCU protocol link.
//
// Covers `{"k":v,...}` where every v is a string, number, true or false. Nested objects and
// arrays are not JSON this link ever produces, so they are rejected rather than parsed. String
// escapes are rejected too: all current command keys and values are ASCII literals, and
// guessing at an escape would be worse than refusing it. No allocation, no exceptions.
#pragma once

#include <cstddef>
#include <string_view>

namespace c3::utils {

/// One member of a flat object: the key and the raw value token, quotes stripped for strings.
struct JsonMember {
  std::string_view key;    // member name without quotes
  std::string_view value;  // string contents, or the bare token for number/true/false
  bool is_string = false;  // true when the value was quoted in the source
};

/// Forward-only scanner over a single flat JSON object held in a string_view.
///
/// Call next() until it returns false, then complete() to confirm the object closed cleanly.
/// Input is one protocol line with its newline already stripped; spaces, tabs and carriage
/// returns are skipped between tokens.
class FlatJsonObject {
public:
  /// Wraps the text to scan; nothing is read until next() is called.
  ///
  /// @param text the object source, which must outlive this scanner and every JsonMember it fills
  explicit constexpr FlatJsonObject(std::string_view text) noexcept : text_(text) {}

  /// Reads the next member.
  ///
  /// Views in `out` point into the source text. Do not call again after a false return.
  ///
  /// @param out receives the key, value and string flag of the member
  /// @return true if a member was read; otherwise false (closing brace reached, or malformed input;
  ///         check failed() to tell the two apart)
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

  /// Reports whether the whole input was one well-formed object.
  ///
  /// Callers should treat anything that is not complete as a protocol error, even when every
  /// next() call succeeded: trailing garbage or a nested value both end up here.
  ///
  /// @return true if the object closed cleanly with nothing but whitespace after it; otherwise false
  [[nodiscard]] bool complete() noexcept {
    skip_ws();
    return closed_ && !failed_ && pos_ >= text_.size();
  }

  /// @return true if malformed input was encountered; otherwise false
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

  // Reads a quoted string into `out` without the quotes. False on a missing quote or an escape.
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

  // Latches the failure so complete() stays false, and returns false for use in a return statement.
  constexpr bool fail() noexcept {
    failed_ = true;
    return false;
  }

  std::string_view text_;
  std::size_t pos_ = 0;
  bool opened_ = false;  // the opening brace has been consumed
  bool closed_ = false;  // the closing brace has been consumed
  bool failed_ = false;  // malformed input was seen; sticky
};

}  // namespace c3::utils
