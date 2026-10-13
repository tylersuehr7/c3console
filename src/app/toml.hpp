// Minimal TOML subset reader for config/console.toml. Init-time only; throws on error.
//
// Supported: `[section]` tables, `key = value`, `#` comments, basic "strings" with
// \" \\ \n \t escapes, integers (decimal, 0x hex, underscores), floats, booleans, and
// single-line arrays of scalars. Not supported: nested tables, inline tables, dotted
// keys, multi-line strings, dates. That is deliberate — keep the dependency list short.
// If the config outgrows this, swap in toml++ rather than extending the parser.
//
// Where the subset is looser than TOML it is lenient, never stricter: a section may be
// reopened (its keys merge; a repeated key is still an error), section names may contain
// dots but are not nested, signs are accepted on hex, and floats may omit a digit on either
// side of the point. CRLF line endings are fine.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace c3::toml {

struct Value;
using Array = std::vector<Value>;

/// One parsed value. Integers and floats are distinct: `1` is an int, `1.0` a float.
struct Value {
  std::variant<bool, std::int64_t, double, std::string, Array> data;

  [[nodiscard]] bool is_bool() const noexcept { return std::holds_alternative<bool>(data); }
  [[nodiscard]] bool is_int() const noexcept { return std::holds_alternative<std::int64_t>(data); }
  [[nodiscard]] bool is_float() const noexcept { return std::holds_alternative<double>(data); }
  [[nodiscard]] bool is_string() const noexcept { return std::holds_alternative<std::string>(data); }
  [[nodiscard]] bool is_array() const noexcept { return std::holds_alternative<Array>(data); }
};

// std::less<> enables lookups by string_view without allocating a key.
using Table = std::map<std::string, Value, std::less<>>;
using Document = std::map<std::string, Table, std::less<>>;  // section -> table; root keys in ""

/// Thrown for malformed input. what() reads "line N: <message>", or "<file>:line N: <message>"
/// when the document came from parse_file(). Lines count from 1.
class ParseError : public std::runtime_error {
public:
  ParseError(std::size_t line, std::string message, std::string file = {});

  [[nodiscard]] std::size_t line() const noexcept { return line_; }
  [[nodiscard]] const std::string& message() const noexcept { return message_; }
  [[nodiscard]] const std::string& file() const noexcept { return file_; }

private:
  std::size_t line_;
  std::string message_;
  std::string file_;
};

/// Parses a whole document. Keys before the first `[section]` land in the "" table; a section
/// header with no keys still creates its table. Throws ParseError.
[[nodiscard]] Document parse(std::string_view text);

/// Throws std::runtime_error if the file cannot be read, ParseError if it is malformed.
[[nodiscard]] Document parse_file(const std::string& path);

/// Lookup helpers. Each returns `fallback` when the section or key is absent and throws
/// std::runtime_error ("[section] key: expected <type>") when the key is present with the
/// wrong type.
[[nodiscard]] std::string get_string(const Document& doc, std::string_view section, std::string_view key,
                                     const std::string& fallback);

[[nodiscard]] std::int64_t get_int(const Document& doc, std::string_view section, std::string_view key,
                                   std::int64_t fallback);

/// Accepts an integer where a float is expected.
[[nodiscard]] double get_float(const Document& doc, std::string_view section, std::string_view key, double fallback);

[[nodiscard]] bool get_bool(const Document& doc, std::string_view section, std::string_view key, bool fallback);

[[nodiscard]] std::vector<std::int64_t> get_int_array(const Document& doc, std::string_view section,
                                                      std::string_view key, const std::vector<std::int64_t>& fallback);

}  // namespace c3::toml
