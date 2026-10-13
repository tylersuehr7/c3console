#include "toml.hpp"

#include <cctype>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>
#include <utility>

namespace c3::toml {
namespace {

// ---------------------------------------------------------------------------
// lexical helpers
// ---------------------------------------------------------------------------

[[nodiscard]] std::string_view trim(std::string_view s) noexcept {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) {
    s.remove_prefix(1);
  }

  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) {
    s.remove_suffix(1);
  }

  return s;
}

// Strip a trailing comment, respecting quoted strings.
[[nodiscard]] std::string_view strip_comment(std::string_view s) noexcept {
  bool in_string = false;
  for (std::size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    if (c == '\\' && in_string) {
      ++i;  // skip the escaped character
      continue;
    }
    if (c == '"') {
      in_string = !in_string;
    } else if (c == '#' && !in_string) {
      return s.substr(0, i);
    }
  }
  return s;
}

[[nodiscard]] bool is_bare_key_char(char c) noexcept {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-';
}

// ---------------------------------------------------------------------------
// value grammar. Every function takes the current line for error reporting.
// ---------------------------------------------------------------------------

[[nodiscard]] std::string parse_string(std::string_view text, std::size_t line) {
  if (text.size() < 2 || text.back() != '"') {
    throw ParseError(line, "unterminated string");
  }

  std::string out;
  for (std::size_t i = 1; i + 1 < text.size(); ++i) {
    char c = text[i];
    if (c == '"') {
      throw ParseError(line, "unexpected quote inside string");
    }

    if (c != '\\') {
      out.push_back(c);
      continue;
    }

    if (i + 2 >= text.size()) {
      throw ParseError(line, "dangling escape");
    }

    c = text[++i];
    switch (c) {
      case 'n':
        out.push_back('\n');
        break;
      case 't':
        out.push_back('\t');
        break;
      case '"':
        out.push_back('"');
        break;
      case '\\':
        out.push_back('\\');
        break;
      default:
        throw ParseError(line, std::string("unsupported escape \\") + c);
    }
  }

  return out;
}

[[nodiscard]] Value parse_number(std::string_view text, std::size_t line) {
  std::string cleaned;
  cleaned.reserve(text.size());
  for (const char c : text) {
    if (c != '_') {
      cleaned.push_back(c);
    }
  }

  std::string_view s = cleaned;
  bool negative = false;
  if (!s.empty() && (s.front() == '-' || s.front() == '+')) {
    negative = s.front() == '-';
    s.remove_prefix(1);
  }

  const bool is_hex = s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
  if (is_hex) {
    s.remove_prefix(2);
  }

  const bool looks_float = !is_hex && s.find_first_of(".eE") != std::string_view::npos;
  if (!looks_float) {
    std::int64_t v = 0;
    const char* const last = std::next(s.data(), static_cast<std::ptrdiff_t>(s.size()));
    const auto res = std::from_chars(s.data(), last, v, is_hex ? 16 : 10);
    if (res.ec != std::errc{} || res.ptr != last) {
      throw ParseError(line, "bad integer '" + std::string(text) + "'");
    }
    return Value{negative ? -v : v};
  }

  // strtod rather than from_chars: floating-point from_chars is missing on some libc++
  // versions we build with. The process never calls setlocale, so the "C" locale applies
  // and '.' is the decimal point.
  const std::string tmp(s);
  char* end = nullptr;
  const double d = std::strtod(tmp.c_str(), &end);
  if (end == nullptr || *end != '\0') {
    throw ParseError(line, "bad float '" + std::string(text) + "'");
  }

  return Value{negative ? -d : d};
}

[[nodiscard]] bool starts_number(std::string_view text) noexcept {
  const char c = text.front();
  return std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '+' || c == '.';
}

// string | bool | number. Arrays hold scalars only, so this is the leaf of the grammar.
[[nodiscard]] Value parse_scalar(std::string_view text, std::size_t line) {
  if (text.front() == '"') {
    return Value{parse_string(text, line)};
  }

  if (text == "true") {
    return Value{true};
  }

  if (text == "false") {
    return Value{false};
  }

  // A bare word (yes, True, inf, a missing quote) is none of the above; say so rather than
  // reporting it as a malformed number.
  if (!starts_number(text)) {
    throw ParseError(line, "bad value '" + std::string(text) + "'");
  }

  return parse_number(text, line);
}

[[nodiscard]] Array parse_array(std::string_view text, std::size_t line) {
  if (text.size() < 2 || text.back() != ']') {
    throw ParseError(line, "unterminated array (arrays must be on one line)");
  }

  const std::string_view body = text.substr(1, text.size() - 2);
  Array out;
  std::size_t start = 0;
  bool in_string = false;

  // Split on commas outside strings. A virtual trailing comma flushes the last element.
  for (std::size_t i = 0; i <= body.size(); ++i) {
    const bool at_end = i == body.size();
    const char c = at_end ? ',' : body[i];
    if (!at_end && c == '\\' && in_string) {
      ++i;
      continue;
    }

    if (c == '"') {
      in_string = !in_string;
    }

    if (c != ',' || in_string) {
      continue;
    }

    const std::string_view item = trim(body.substr(start, i - start));
    start = i + 1;

    if (item.empty()) {
      if (at_end) {
        continue;  // empty array or a trailing comma, both legal
      }
      throw ParseError(line, "empty array element");
    }

    if (item.front() == '[') {
      throw ParseError(line, "nested arrays are not supported");
    }

    out.push_back(parse_scalar(item, line));
  }

  if (in_string) {
    throw ParseError(line, "unterminated string");  // the closing quote was never seen
  }

  return out;
}

[[nodiscard]] Value parse_value(std::string_view text, std::size_t line) {
  text = trim(text);

  if (text.empty()) {
    throw ParseError(line, "missing value");
  }

  if (text.front() == '[') {
    return Value{parse_array(text, line)};
  }

  return parse_scalar(text, line);
}

// ---------------------------------------------------------------------------
// line grammar
// ---------------------------------------------------------------------------

// "[name]" -> name. Bare names with dots only.
[[nodiscard]] std::string parse_section_header(std::string_view line_text, std::size_t line) {
  if (line_text.size() < 3 || line_text.back() != ']') {
    throw ParseError(line, "malformed section header");
  }

  if (line_text[1] == '[') {
    throw ParseError(line, "arrays of tables are not supported");
  }

  const std::string_view name = trim(line_text.substr(1, line_text.size() - 2));
  for (const char c : name) {
    if (!is_bare_key_char(c) && c != '.') {
      throw ParseError(line, "bad section name");
    }
  }

  return std::string(name);
}

// "key = value" into `table`.
void parse_key_value(std::string_view line_text, std::size_t line, Table& table) {
  const std::size_t eq = line_text.find('=');
  if (eq == std::string_view::npos) {
    throw ParseError(line, "expected key = value");
  }

  const std::string_view key = trim(line_text.substr(0, eq));
  if (key.empty()) {
    throw ParseError(line, "empty key");
  }

  for (const char c : key) {
    if (!is_bare_key_char(c)) {
      throw ParseError(line, "only bare keys are supported");
    }
  }

  Value value = parse_value(line_text.substr(eq + 1), line);
  const auto [it, inserted] = table.try_emplace(std::string(key), std::move(value));
  if (!inserted) {
    throw ParseError(line, "duplicate key '" + std::string(key) + "'");
  }
}

// ---------------------------------------------------------------------------
// lookup
// ---------------------------------------------------------------------------

[[nodiscard]] const Value* find(const Document& doc, std::string_view section, std::string_view key) noexcept {
  const auto sec = doc.find(section);
  if (sec == doc.end()) {
    return nullptr;
  }
  const auto it = sec->second.find(key);
  return it == sec->second.end() ? nullptr : &it->second;
}

[[noreturn]] void wrong_type(std::string_view section, std::string_view key, std::string_view expected) {
  throw std::runtime_error("[" + std::string(section) + "] " + std::string(key) + ": expected " +
                           std::string(expected));
}

// Fetch a scalar of type T or throw with `expected` in the message.
template <typename T>
[[nodiscard]] T get_scalar(const Document& doc, std::string_view section, std::string_view key, const T& fallback,
                           std::string_view expected) {
  const Value* v = find(doc, section, key);

  if (v == nullptr) {
    return fallback;
  }

  if (!std::holds_alternative<T>(v->data)) {
    wrong_type(section, key, expected);
  }

  return std::get<T>(v->data);
}

}  // namespace

ParseError::ParseError(std::size_t line, std::string message, std::string file)
    : std::runtime_error((file.empty() ? std::string{} : file + ":") + "line " + std::to_string(line) + ": " + message),
      line_(line),
      message_(std::move(message)),
      file_(std::move(file)) {}

Document parse(std::string_view text) {
  Document doc;
  std::string current;  // "" until the first [section]
  std::size_t line_no = 0;
  std::size_t pos = 0;
  while (pos <= text.size()) {
    std::size_t nl = text.find('\n', pos);
    if (nl == std::string_view::npos) {
      nl = text.size();
    }
    const std::string_view raw = text.substr(pos, nl - pos);
    pos = nl + 1;
    ++line_no;

    const std::string_view line = trim(strip_comment(raw));
    if (line.empty()) {
      continue;
    }
    if (line.front() == '[') {
      current = parse_section_header(line, line_no);
      doc.try_emplace(current);  // a section with no keys is still a section
      continue;
    }
    parse_key_value(line, line_no, doc[current]);
  }
  return doc;
}

Document parse_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("cannot open " + path);
  }

  // A directory opens but reads as empty, which would silently pass as an empty document.
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) {
    throw std::runtime_error("cannot read " + path + ": not a regular file");
  }

  std::stringstream buf;
  buf << in.rdbuf();
  try {
    return parse(buf.str());
  } catch (const ParseError& e) {
    throw ParseError(e.line(), e.message(), path);
  }
}

std::string get_string(const Document& doc, std::string_view section, std::string_view key,
                       const std::string& fallback) {
  return get_scalar<std::string>(doc, section, key, fallback, "string");
}

std::int64_t get_int(const Document& doc, std::string_view section, std::string_view key, std::int64_t fallback) {
  return get_scalar<std::int64_t>(doc, section, key, fallback, "integer");
}

double get_float(const Document& doc, std::string_view section, std::string_view key, double fallback) {
  const Value* v = find(doc, section, key);
  if (v != nullptr && v->is_int()) {
    return static_cast<double>(std::get<std::int64_t>(v->data));
  }
  return get_scalar<double>(doc, section, key, fallback, "number");
}

bool get_bool(const Document& doc, std::string_view section, std::string_view key, bool fallback) {
  return get_scalar<bool>(doc, section, key, fallback, "boolean");
}

std::vector<std::int64_t> get_int_array(const Document& doc, std::string_view section, std::string_view key,
                                        const std::vector<std::int64_t>& fallback) {
  const Value* v = find(doc, section, key);
  if (v == nullptr) {
    return fallback;
  }

  if (!v->is_array()) {
    wrong_type(section, key, "array of integers");
  }

  const auto& items = std::get<Array>(v->data);
  std::vector<std::int64_t> out;
  out.reserve(items.size());

  for (const Value& item : items) {
    if (!item.is_int()) {
      wrong_type(section, key, "array of integers");
    }
    out.push_back(std::get<std::int64_t>(item.data));
  }

  return out;
}

}  // namespace c3::toml
