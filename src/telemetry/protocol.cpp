#include "protocol.hpp"

#include "crc16.hpp"
#include "fault_codes.hpp"
#include "limits.hpp"
#include "utils/parse_number.hpp"

#include <charconv>
#include <cstdlib>
#include <iterator>

namespace c3::telemetry {

namespace {
constexpr std::string_view kCrcMarker = R"(,"crc":")";
constexpr std::size_t kCrcDigits = 4;

/// Super minimal JSON scanner over a string_view; every function consumers from `s`.
class Scanner {
public:
  explicit Scanner(std::string_view s) noexcept : s_(s) {}

  void skip_ws() noexcept {
    while (!s_.empty() && (s_.front() == ' ' || s_.front() == '\t')) {
      s_.remove_prefix(1);
    }
  }

  [[nodiscard]] bool accept(char c) noexcept {
    skip_ws();
    if (s_.empty() || s_.front() != c) {
      return false;
    }
    s_.remove_prefix(1);
    return true;
  }

  [[nodiscard]] bool peek(char c) noexcept {
    skip_ws();
    return !s_.empty() && s_.front() == c;
  }

  [[nodiscard]] bool at_end() noexcept {
    skip_ws();
    return s_.empty();
  }

  /// "..." without escapes (the protocol never needs them).
  [[nodiscard]] bool string(std::string_view& out) noexcept {
    if (!accept('"')) {
      return false;
    }

    const std::size_t end = s_.find('"');
    if (end == std::string_view::npos || s_.substr(0, end).find('\\') != std::string_view::npos) {
      return false;
    }

    out = s_.substr(0, end);
    s_.remove_prefix(end + 1);
    return true;
  }

  // JSON number token: -?digits[.digits][e[+-]digits]
  [[nodiscard]] bool number_token(std::string_view& out) noexcept {
    skip_ws();
    std::size_t n = 0;
    const auto is_num_char = [](char c) noexcept {
      return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E';
    };

    while (n < s_.size() && is_num_char(s_[n])) {
      ++n;
    }

    if (n == 0) {
      return false;
    }

    out = s_.substr(0, n);
    s_.remove_prefix(n);
    return true;
  }

  [[nodiscard]] bool boolean(bool& out) noexcept {
    skip_ws();

    if (s_.starts_with("true")) {
      s_.remove_prefix(4);
      out = true;
      return true;
    }

    if (s_.starts_with("false")) {
      s_.remove_prefix(5);
      out = false;
      return true;
    }

    return false;
  }

  // Skips any value (for keys we do not know): a scalar, or a container tree up
  // to a fixed nesting depth, walked with an explicit stack so garbage cannot
  // recurse.
  [[nodiscard]] bool skip_value() noexcept {
    SkipStack st;
    for (;;) {
      skip_ws();

      if (s_.empty()) {
        return false;
      }

      const char c = s_.front();
      if (c == '{' || c == '[') {
        bool empty = false;
        if (!open_container(st, c, empty)) {
          return false;
        }
        if (!empty) {
          continue;  // a member or element follows
        }
      } else if (!skip_scalar()) {
        return false;
      }

      if (!close_or_next(st)) {
        return false;
      }

      if (st.depth == 0) {
        return true;
      }
    }
  }

private:
  struct SkipStack {
    std::array<char, 5> closers{};  // ']' or '}' per-open level
    std::size_t depth = 0;
  };

  // Consumes '{' or '['. An empty container is closed immediately and reported through
  // `empty` so the caller moves on to the separator instead of expecting a value.
  [[nodiscard]] bool open_container(SkipStack& st, char opener, bool& empty) noexcept {
    if (st.depth == st.closers.size()) {
      return false;  // deeper than any frame we would ever send
    }

    const char closer = opener == '{' ? '}' : ']';
    st.closers[st.depth++] = closer;
    s_.remove_prefix(1);

    empty = accept(closer);
    if (empty) {
      --st.depth;
      return true;
    }

    return closer != '}' || member_key();
  }

  // After a value: pops every level that closes here, then consumes the
  // separator to the next value (and its key, inside an object). Depth 0
  // afterwards means the value is done.
  [[nodiscard]] bool close_or_next(SkipStack& st) noexcept {
    while (st.depth > 0) {
      if (accept(st.closers[st.depth - 1])) {
        --st.depth;
        continue;
      }

      if (!accept(',')) {
        return false;
      }

      return st.closers[st.depth - 1] != '}' || member_key();
    }
    return true;
  }

  [[nodiscard]] bool member_key() noexcept {
    std::string_view key;
    return string(key) && accept(':');
  }

  [[nodiscard]] bool skip_scalar() noexcept {
    std::string_view sv;
    bool b = false;

    if (s_.front() == '"') {
      return string(sv);
    }

    if (s_.starts_with("null")) {
      s_.remove_prefix(4);
      return true;
    }

    return boolean(b) || number_token(sv);
  }

  std::string_view s_;
};

[[nodiscard]] bool hex4(std::string_view tok, std::uint16_t& out) noexcept {
  if (tok.size() != kCrcDigits) {
    return false;
  }

  const char* const last = std::next(tok.data(), static_cast<std::ptrdiff_t>(tok.size()));
  const auto res = std::from_chars(tok.data(), last, out, 16);

  return res.ec == std::errc{} && res.ptr == last;
}

[[nodiscard]] bool in_range(float v, float lo, float hi) noexcept {
  return v >= lo && v <= hi;
}

// Required-field bitmask.
constexpr std::uint8_t kSeenT = 1U << 0U;
constexpr std::uint8_t kSeenFuel = 1U << 1U;
constexpr std::uint8_t kSeenOil = 1U << 2U;
constexpr std::uint8_t kSeenClt = 1U << 3U;
constexpr std::uint8_t kSeenVolt = 1U << 4U;
constexpr std::uint8_t kSeenFaults = 1U << 5U;
constexpr std::uint8_t kSeenAll = kSeenT | kSeenFuel | kSeenOil | kSeenClt | kSeenVolt | kSeenFaults;

[[nodiscard]] ParseError parse_faults(Scanner& sc, Frame& f) noexcept {
  if (!sc.accept('[')) {
    return ParseError::Malformed;
  }

  if (sc.accept(']')) {
    return ParseError::None;
  }

  for (;;) {
    std::string_view code;
    if (!sc.string(code)) {
      return ParseError::Malformed;
    }

    const auto fault = parse_fault_code(code);
    if (!fault) {
      return ParseError::UnknownFault;
    }

    if (!f.faults.push_back(*fault)) {
      return ParseError::TooManyFaults;
    }

    if (sc.accept(']')) {
      return ParseError::None;
    }

    if (!sc.accept(',')) {
      return ParseError::Malformed;
    }
  }
}

[[nodiscard]] ParseError parse_raw(Scanner& sc, Frame& f) noexcept {
  if (!sc.accept('{')) {
    return ParseError::Malformed;
  }

  if (sc.accept('}')) {
    return ParseError::None;
  }

  for (;;) {
    std::string_view key;
    std::string_view tok;

    if (!sc.string(key) || !sc.accept(':') || !sc.number_token(tok)) {
      return ParseError::Malformed;
    }

    std::int32_t v = 0;
    if (!utils::parse_int(tok, v)) {
      return ParseError::Malformed;
    }

    if (const auto ch = parse_channel_key(key)) {
      f.raw[static_cast<std::size_t>(*ch)] = v;
    }  // unknown raw keys are skipped

    if (sc.accept('}')) {
      return ParseError::None;
    }

    if (!sc.accept(',')) {
      return ParseError::Malformed;
    }
  }
}

[[nodiscard]] ParseError parse_relay_field(std::string_view key, Scanner& sc, vehicle::RelayState& r) noexcept {
  if (key == "blower") {
    std::string_view tok;
    std::uint32_t v = 0;

    if (!sc.number_token(tok) || !utils::parse_int(tok, v)) {
      return ParseError::Malformed;
    }

    if (v > 4) {
      return ParseError::OutOfRange;
    }

    r.blower = static_cast<vehicle::HvacBlower>(v);
    return ParseError::None;
  }

  if (key == "ac") {
    return sc.boolean(r.ac_enabled) ? ParseError::None : ParseError::Malformed;
  }

  if (key == "doors") {
    bool value = false;
    if (!sc.boolean(value)) {
      return ParseError::Malformed;
    }
    r.lamp_doors_open = value;
    return ParseError::None;
  }

  if (key == "headlights") {
    return sc.boolean(r.headlights_on) ? ParseError::None : ParseError::Malformed;
  }

  if (key == "high") {
    bool value = false;
    if (!sc.boolean(value)) {
      return ParseError::Malformed;
    }
    r.highbeams_on = value;
    return ParseError::None;
  }

  if (key == "mode") {
    std::string_view mode;
    if (!sc.string(mode)) {
      return ParseError::Malformed;
    }
    if (mode == "vent") {
      r.mode = vehicle::HvacMode::Vent;
    } else if (mode == "floor") {
      r.mode = vehicle::HvacMode::Floor;
    } else if (mode == "defrost") {
      r.mode = vehicle::HvacMode::Defrost;
    } else {
      return ParseError::OutOfRange;
    }
    return ParseError::None;
  }

  return sc.skip_value() ? ParseError::None : ParseError::Malformed;
}

[[nodiscard]] ParseError parse_relays(Scanner& sc, Frame& f) noexcept {
  if (!sc.accept('{')) {
    return ParseError::Malformed;
  }

  vehicle::RelayState r;
  if (sc.accept('}')) {
    return ParseError::MissingField;
  }

  constexpr std::array<std::string_view, 6> kFields = {"blower", "ac", "mode", "headlights", "doors", "high"};
  std::uint8_t seen = 0;

  for (;;) {
    std::string_view key;

    if (!sc.string(key) || !sc.accept(':')) {
      return ParseError::Malformed;
    }

    for (std::size_t i = 0; i < kFields.size(); ++i) {
      if (key == kFields[i]) {
        const auto bit = static_cast<std::uint8_t>(1U << i);
        if ((seen & bit) != 0) {
          return ParseError::Malformed;
        }
        seen |= bit;
      }
    }

    if (const ParseError err = parse_relay_field(key, sc, r); err != ParseError::None) {
      return err;
    }

    if (sc.accept('}')) {
      // The object is optional, but a present object must describe all established outputs.
      if ((seen & 0x0FU) != 0x0FU) {
        return ParseError::MissingField;
      }
      f.relays = r;
      return ParseError::None;
    }

    if (!sc.accept(',')) {
      return ParseError::Malformed;
    }
  }
}

// One of the five scalar fields. Range checks are wire plausibility, not warning thresholds.
[[nodiscard]] ParseError parse_scalar_field(std::string_view key, Scanner& sc, Frame& f, std::uint8_t& seen) noexcept {
  std::string_view tok;

  if (!sc.number_token(tok)) {
    return ParseError::Malformed;
  }

  if (key == "t") {
    seen |= kSeenT;
    return utils::parse_int(tok, f.t_ms) ? ParseError::None : ParseError::Malformed;
  }

  float v = 0.0F;
  if (!utils::parse_float(tok, v)) {
    return ParseError::Malformed;
  }

  struct Field {
    float* target;
    float lo;
    float hi;
    std::uint8_t bit;
  };

  const Field field = key == "fuel"  ? Field{&f.fuel_fraction, limits::kFuelMin, limits::kFuelMax, kSeenFuel}
                      : key == "oil" ? Field{&f.oil_psi, limits::kOilMinPsi, limits::kOilMaxPsi, kSeenOil}
                      : key == "clt" ? Field{&f.coolant_f, limits::kCltMinF, limits::kCltMaxF, kSeenClt}
                                     : Field{&f.voltage_v, limits::kVoltMin, limits::kVoltMax, kSeenVolt};
  *field.target = v;
  seen |= field.bit;

  return in_range(v, field.lo, field.hi) ? ParseError::None : ParseError::OutOfRange;
}

// Locates and verifies the trailing crc field. On success returns the body: the object text
// up to (not including) the field, whose closing brace was moved after the crc by the MCU.
[[nodiscard]] std::optional<std::string_view> check_crc(std::string_view line, ParseError& err) noexcept {
  const std::size_t crc_at = line.rfind(kCrcMarker);
  if (crc_at == std::string_view::npos) {
    err = ParseError::MissingCrc;
    return std::nullopt;
  }

  const std::string_view after = line.substr(crc_at + kCrcMarker.size());
  std::uint16_t expected = 0;
  if (after.size() != kCrcDigits + 2 || !hex4(after.substr(0, kCrcDigits), expected) ||
      after.substr(kCrcDigits) != "\"}") {
    err = ParseError::BadCrcField;
    return std::nullopt;
  }

  Crc16Ccitt crc;
  crc.update(line.substr(0, crc_at));
  crc.update("}");

  if (crc.value() != expected) {
    err = ParseError::CrcMismatch;
    return std::nullopt;
  }

  err = ParseError::None;
  return line.substr(0, crc_at);
}
}  // namespace

// Maps the raw-object keys ("fuel", "oil", "clt", "volt") to channels.
std::optional<Channel> parse_channel_key(std::string_view key) noexcept {
  if (key == "fuel") {
    return Channel::Fuel;
  } else if (key == "oil") {
    return Channel::Oil;
  } else if (key == "clt") {
    return Channel::Coolant;
  } else if (key == "volt") {
    return Channel::Volt;
  }
  return std::nullopt;
}

ParseResult parse_frame(std::string_view line) noexcept {
  if (!line.empty() && line.back() == '\r') {
    line.remove_suffix(1);
  }

  if (line.empty()) {
    return {.frame = std::nullopt, .error = ParseError::Empty};
  }

  if (line.size() > kMaxLineLength) {
    return {.frame = std::nullopt, .error = ParseError::TooLong};
  }

  ParseError err = ParseError::None;
  const std::optional<std::string_view> body = check_crc(line, err);
  if (!body) {
    return {.frame = std::nullopt, .error = err};
  }

  Scanner sc(*body);
  Frame f;
  std::uint8_t seen = 0;

  if (!sc.accept('{')) {
    return {.frame = std::nullopt, .error = ParseError::Malformed};
  }

  for (;;) {
    std::string_view key;

    if (!sc.string(key) || !sc.accept(':')) {
      return {.frame = std::nullopt, .error = ParseError::Malformed};
    }

    if (key == "t" || key == "fuel" || key == "oil" || key == "clt" || key == "volt") {
      err = parse_scalar_field(key, sc, f, seen);
    } else if (key == "faults") {
      err = parse_faults(sc, f);
      seen |= kSeenFaults;
    } else if (key == "raw") {
      err = parse_raw(sc, f);
    } else if (key == "relays") {
      err = parse_relays(sc, f);
    } else if (key == "rev") {
      err = sc.boolean(f.reverse) ? ParseError::None : ParseError::Malformed;
    } else if (!sc.skip_value()) {
      err = ParseError::Malformed;
    }

    if (err != ParseError::None) {
      return {.frame = std::nullopt, .error = err};
    }

    if (sc.at_end()) {
      break;  // the closing brace travelled after the crc field; check_crc accounted for it
    }

    if (!sc.accept(',')) {
      return {.frame = std::nullopt, .error = ParseError::Malformed};
    }
  }

  if ((seen & kSeenAll) != kSeenAll) {
    return {.frame = std::nullopt, .error = ParseError::MissingField};
  }

  return {.frame = f, .error = ParseError::None};
}

}  // namespace c3::telemetry