#include "calibration.hpp"

#include "fault_codes.hpp"
#include "protocol.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace c3::telemetry {

namespace {

using Json = nlohmann::json;

constexpr int kSupportedFormat = 1;

// Every rejection is a std::runtime_error carrying the message; parse_calibration turns it
// into CalibrationLoadResult::error. Exceptions stay inside this init-time file.
[[noreturn]] void reject(const std::string& message) {
  throw std::runtime_error(message);
}

[[nodiscard]] const Json& require(const Json& obj, const char* key, const std::string& where) {
  const auto it = obj.find(key);
  if (it == obj.end()) {
    reject(where + ": missing \"" + key + "\"");
  }
  return *it;
}

[[nodiscard]] float finite_float(const Json& v, const std::string& where) {
  if (!v.is_number()) {
    reject(where + ": expected a number");
  }

  const double d = v.get<double>();
  if (!std::isfinite(d) || std::fabs(d) > std::numeric_limits<float>::max()) {
    reject(where + ": not a finite value");
  }

  return static_cast<float>(d);
}

[[nodiscard]] std::int32_t raw_count(const Json& v, const std::string& where) {
  if (!v.is_number_integer()) {
    reject(where + ": \"raw\" must be an integer ADC count");
  }

  const auto n = v.get<std::int64_t>();
  if (n < std::numeric_limits<std::int32_t>::min() || n > std::numeric_limits<std::int32_t>::max()) {
    reject(where + ": \"raw\" out of int32 range");
  }

  return static_cast<std::int32_t>(n);
}

[[nodiscard]] CalibrationCurve parse_curve(const Json& channel, const std::string& where) {
  if (!channel.is_object()) {
    reject(where + R"(: expected an object with a "points" array)");
  }

  const Json& points = require(channel, "points", where);
  if (!points.is_array()) {
    reject(where + ": \"points\" must be an array");
  }

  if (points.size() > kMaxCalibrationPoints) {
    reject(where + ": more than " + std::to_string(kMaxCalibrationPoints) + " points");
  }

  CalibrationCurve::Points list;
  for (std::size_t i = 0; i < points.size(); ++i) {
    const std::string at = where + ".points[" + std::to_string(i) + "]";
    const Json& p = points[i];
    if (!p.is_object()) {
      reject(at + R"(: expected {"raw": int, "value": number})");
    }
    list.push_back({.raw = raw_count(require(p, "raw", at), at), .value = finite_float(require(p, "value", at), at)});
  }

  const auto curve = CalibrationCurve::from_points(list);
  if (!curve) {
    reject(where + ": needs at least 2 points with strictly increasing \"raw\"");
  }

  return *curve;
}

[[nodiscard]] CalibrationTable parse_document(const Json& doc) {
  if (!doc.is_object()) {
    reject("calibration: top level must be an object");
  }

  const Json& format = require(doc, "format", "calibration");
  if (!format.is_number_integer() || format.get<int>() != kSupportedFormat) {
    reject("calibration: \"format\" must be " + std::to_string(kSupportedFormat));
  }

  const Json& placeholder = require(doc, "placeholder", "calibration");
  if (!placeholder.is_boolean()) {
    reject("calibration: \"placeholder\" must be true or false");
  }

  const Json& channels = require(doc, "channels", "calibration");
  if (!channels.is_object()) {
    reject("calibration: \"channels\" must be an object");
  }

  CalibrationTable table;
  table.placeholder = placeholder.get<bool>();
  std::array<bool, kChannelCount> seen{};
  for (const auto& [key, channel] : channels.items()) {
    const auto ch = parse_channel_key(key);
    if (!ch) {
      reject("calibration: unknown channel \"" + key + "\" (expected fuel, oil, clt, volt)");
    }
    const auto idx = static_cast<std::size_t>(*ch);
    seen[idx] = true;
    table.curves[idx] = parse_curve(channel, "channels." + key);
  }

  for (std::size_t i = 0; i < kChannelCount; ++i) {
    const auto ch = static_cast<Channel>(i);
    if (!seen[i]) {
      reject(std::string("calibration: channel \"") + std::string(to_string(ch)) + "\" is missing");
    }
    // Safety invariant 5: a file may only call itself calibrated when every channel has
    // the bench sweep the spec asks for. Flipping the flag by hand is not calibration.
    if (!table.placeholder && table.curves[i].size() < kMinMeasuredPoints) {
      reject(std::string(R"(calibration: "placeholder" is false but channel ")") + std::string(to_string(ch)) +
             R"(" has )" + std::to_string(table.curves[i].size()) + " points; spec §9.2 requires at least " +
             std::to_string(kMinMeasuredPoints) +
             " measured points per channel before the placeholder flag may be cleared");
    }
  }

  return table;
}

}  // namespace

CalibrationLoadResult parse_calibration(std::string_view json_text) noexcept {
  try {
    const Json doc = Json::parse(json_text, nullptr, /*allow_exceptions=*/true, /*ignore_comments=*/false);
    return {.table = parse_document(doc), .error = {}};
  } catch (const Json::exception& e) {
    return {.table = std::nullopt, .error = std::string("calibration: invalid JSON: ") + e.what()};
  } catch (const std::exception& e) {
    return {.table = std::nullopt, .error = e.what()};
  }
}

CalibrationLoadResult load_calibration(const std::string& path) noexcept {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {.table = std::nullopt, .error = "calibration: cannot open " + path};
  }

  std::ostringstream text;
  text << in.rdbuf();

  CalibrationLoadResult result = parse_calibration(text.str());
  if (!result.table) {
    result.error += " (" + path + ")";
  }

  return result;
}

std::string describe(const CalibrationTable& table) noexcept {
  std::ostringstream os;
  os << (table.placeholder ? "PLACEHOLDER (identity-shaped, not calibrated)" : "measured");
  os << "; points:";

  for (std::size_t i = 0; i < kChannelCount; ++i) {
    os << ' ' << to_string(static_cast<Channel>(i)) << '=' << table.curves[i].size();
  }

  return os.str();
}

}  // namespace c3::telemetry
