#include "csv_codec.hpp"

#include "fault_codes.hpp"
#include "limits.hpp"
#include "utils/parse_number.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

namespace c3::telemetry {

namespace {
constexpr std::size_t kFieldCount = 10;

[[nodiscard]] std::string_view strip_cr(std::string_view s) noexcept {
  if (!s.empty() && s.back() == '\r') {
    s.remove_suffix(1);
  }
  return s;
}

// Splits on commas into exactly kFieldCount views; false on any other count.
[[nodiscard]] bool split_fields(std::string_view row, std::array<std::string_view, kFieldCount>& out) noexcept {
  std::size_t n = 0;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= row.size(); ++i) {
    if (i == row.size() || row[i] == ',') {
      if (n == kFieldCount) {
        return false;
      }
      out[n++] = row.substr(start, i - start);
      start = i + 1;
    }
  }
  return n == kFieldCount;
}

[[nodiscard]] CsvError parse_bounded(std::string_view tok, float lo, float hi, float& out) noexcept {
  if (!utils::parse_float(tok, out)) {
    return CsvError::BadNumber;
  }
  return (out >= lo && out <= hi) ? CsvError::None : CsvError::OutOfRange;
}

[[nodiscard]] CsvError parse_faults(std::string_view list, Frame& frame) noexcept {
  frame.faults.clear();

  if (list.empty()) {
    return CsvError::None;
  }

  std::size_t start = 0;
  for (std::size_t i = 0; i <= list.size(); ++i) {
    if (i == list.size() || list[i] == kCsvFaultDelimiter) {
      const auto fault = parse_fault_code(list.substr(start, i - start));
      if (!fault) {
        return CsvError::UnknownFault;
      }

      if (!frame.faults.push_back(*fault)) {
        return CsvError::TooManyFaults;
      }

      start = i + 1;
    }
  }

  return CsvError::None;
}
}  // namespace

std::size_t format_csv_row(const Frame& frame, std::span<char> out) noexcept {
  const int n = std::snprintf(out.data(), out.size(), "%u,%d,%d,%d,%d,%.4f,%.2f,%.2f,%.3f,", frame.t_ms, frame.raw[0],
                              frame.raw[1], frame.raw[2], frame.raw[3], static_cast<double>(frame.fuel_fraction),
                              static_cast<double>(frame.oil_psi), static_cast<double>(frame.coolant_f),
                              static_cast<double>(frame.voltage_v));

  if (n <= 0 || static_cast<std::size_t>(n) >= out.size()) {
    return 0;
  }

  auto len = static_cast<std::size_t>(n);
  std::array<char, kMaxFaultCodeLength> code{};
  for (std::size_t i = 0; i < frame.faults.size(); ++i) {
    const std::size_t clen = format_fault_code(frame.faults[i], code);
    const std::size_t need = clen + (i > 0 ? 1 : 0);

    if (len + need >= out.size()) {
      return 0;
    }

    if (i > 0) {
      out[len++] = kCsvFaultDelimiter;
    }

    std::copy_n(code.begin(), clen, out.begin() + static_cast<std::ptrdiff_t>(len));
    len += clen;
  }

  return len;
}

CsvError parse_csv_row(std::string_view row, Frame& out) noexcept {
  std::array<std::string_view, kFieldCount> fields{};
  if (!split_fields(strip_cr(row), fields)) {
    return CsvError::FieldCount;
  }

  Frame f;
  if (!utils::parse_int(fields[0], f.t_ms)) {
    return CsvError::BadNumber;
  }

  for (std::size_t i = 0; i < kChannelCount; ++i) {
    if (!utils::parse_int(fields[1 + i], f.raw[i])) {
      return CsvError::BadNumber;
    }
  }

  if (const auto e = parse_bounded(fields[5], limits::kFuelMin, limits::kFuelMax, f.fuel_fraction);
      e != CsvError::None) {
    return e;
  }

  if (const auto e = parse_bounded(fields[6], limits::kOilMinPsi, limits::kOilMaxPsi, f.oil_psi); e != CsvError::None) {
    return e;
  }

  if (const auto e = parse_bounded(fields[7], limits::kCltMinF, limits::kCltMaxF, f.coolant_f); e != CsvError::None) {
    return e;
  }

  if (const auto e = parse_bounded(fields[8], limits::kVoltMin, limits::kVoltMax, f.voltage_v); e != CsvError::None) {
    return e;
  }

  if (const auto e = parse_faults(fields[9], f); e != CsvError::None) {
    return e;
  }

  out = f;
  return CsvError::None;
}

bool is_csv_header(std::string_view line) noexcept {
  return strip_cr(line) == kCsvHeader;
}

}  // namespace c3::telemetry