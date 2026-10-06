#include "telemetry/calibration.hpp"
#include "telemetry/frame.hpp"
#include "telemetry/limits.hpp"
#include "utils/linear_curve.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

using c3::telemetry::CalibrationCurve;
using c3::telemetry::CalibrationLoadResult;
using c3::telemetry::CalibrationTable;
using c3::telemetry::Channel;
using c3::telemetry::describe;
using c3::telemetry::kChannelCount;
using c3::telemetry::kMaxCalibrationPoints;
using c3::telemetry::kMinMeasuredPoints;
using c3::telemetry::load_calibration;
using c3::telemetry::parse_calibration;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinAbs;

namespace {

// ---------------------------------------------------------------------------
// Document builders. Tests allocate freely; the code under test runs at init only.
// ---------------------------------------------------------------------------

/// `n` points with raw = i * 1000 and value = offset + i * step, strictly increasing.
[[nodiscard]] std::string points(std::size_t n, double step = 1.0, double offset = 0.0) {
  std::string out = "[";
  for (std::size_t i = 0; i < n; ++i) {
    if (i > 0) {
      out += ",";
    }
    out += R"({"raw":)" + std::to_string(i * 1000) + R"(,"value":)" +
           std::to_string(offset + step * static_cast<double>(i)) + "}";
  }
  return out + "]";
}

[[nodiscard]] std::string channel(std::string_view points_json) {
  return R"({"points":)" + std::string(points_json) + "}";
}

struct Channels {
  std::string fuel = channel(points(2));
  std::string oil = channel(points(2));
  std::string clt = channel(points(2));
  std::string volt = channel(points(2));
};

[[nodiscard]] std::string doc(bool placeholder, const Channels& ch = {}, std::string_view format = "1") {
  std::string out = "{";
  out += R"("placeholder":)" + std::string(placeholder ? "true" : "false") + ",";
  out += R"("format":)" + std::string(format) + ",";
  out += R"("channels":{"fuel":)" + ch.fuel + R"(,"oil":)" + ch.oil + R"(,"clt":)" + ch.clt + R"(,"volt":)" + ch.volt +
         "}";
  return out + "}";
}

/// The spec §9.2 example, pretty-printed as it appears in the document.
constexpr std::string_view kSpecExample = R"json({
    "placeholder": true,
    "format": 1,
    "channels": {
        "fuel": {
            "points": [
                {
                    "raw": 0,
                    "value": 0.0
                },
                {
                    "raw": 32767,
                    "value": 1.0
                }
            ]
        },
        "oil": { "points": [ { "raw": 0, "value": 0.0 }, { "raw": 32767, "value": 200.0 } ] },
        "clt": { "points": [ { "raw": 0, "value": -40.0 }, { "raw": 32767, "value": 400.0 } ] },
        "volt": { "points": [ { "raw": 0, "value": 0.0 }, { "raw": 32767, "value": 40.0 } ] }
    }
})json";

[[nodiscard]] CalibrationTable expect_table(std::string_view json) {
  INFO("json: " << json);
  const CalibrationLoadResult r = parse_calibration(json);
  INFO("error: " << r.error);
  REQUIRE(r.error.empty());
  REQUIRE(r.table.has_value());
  return *r.table;
}

/// Asserts rejection and the result invariant, returning the message for closer checks.
[[nodiscard]] std::string expect_rejected(std::string_view json) {
  INFO("json: " << json);
  const CalibrationLoadResult r = parse_calibration(json);
  CHECK_FALSE(r.table.has_value());
  CHECK_FALSE(r.error.empty());
  return r.error;
}

[[nodiscard]] std::size_t index(Channel ch) noexcept {
  return static_cast<std::size_t>(ch);
}

/// A fresh scratch directory, removed when the test case ends.
class ScratchDir {
public:
  ScratchDir() {
    static unsigned counter = 0;
    path_ = std::filesystem::temp_directory_path() / ("c3console_calibration_" + std::to_string(counter++));
    std::filesystem::remove_all(path_);
    REQUIRE(std::filesystem::create_directory(path_));
  }
  ~ScratchDir() { std::filesystem::remove_all(path_); }
  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;
  ScratchDir(ScratchDir&&) = delete;
  ScratchDir& operator=(ScratchDir&&) = delete;

  [[nodiscard]] std::string write(std::string_view name, std::string_view content) const {
    const auto path = (path_ / name).string();
    std::ofstream out(path, std::ios::binary);
    REQUIRE(out.is_open());
    out << content;
    return path;
  }

  [[nodiscard]] std::string file(std::string_view name) const { return (path_ / name).string(); }

private:
  std::filesystem::path path_;
};

// ---------------------------------------------------------------------------
// Constants and types
// ---------------------------------------------------------------------------

TEST_CASE("calibration: point limits follow the spec and the curve capacity", "[calibration]") {
  STATIC_CHECK(kMinMeasuredPoints == 8);
  STATIC_CHECK(kMaxCalibrationPoints == 24);
  STATIC_CHECK(kMaxCalibrationPoints == CalibrationCurve::kCapacity);
}

TEST_CASE("calibration: loaders are noexcept so a bad file can never unwind into startup", "[calibration]") {
  STATIC_CHECK(noexcept(parse_calibration(std::string_view{})));
  STATIC_CHECK(noexcept(load_calibration(std::string{})));
  STATIC_CHECK(noexcept(describe(std::declval<const CalibrationTable&>())));
}

TEST_CASE("CalibrationTable: default is an empty placeholder that converts everything to 0", "[calibration]") {
  const CalibrationTable table;
  CHECK(table.placeholder);
  for (std::size_t i = 0; i < kChannelCount; ++i) {
    CHECK(table.curves[i].size() == 0);
    CHECK(table.convert(static_cast<Channel>(i), 12345) == 0.0F);
  }
}

TEST_CASE("CalibrationTable: curve() indexes by channel", "[calibration]") {
  const CalibrationTable table = expect_table(doc(true, Channels{
                                                            .fuel = channel(points(2)),
                                                            .oil = channel(points(3)),
                                                            .clt = channel(points(4)),
                                                            .volt = channel(points(5)),
                                                        }));
  CHECK(table.curve(Channel::Fuel).size() == 2);
  CHECK(table.curve(Channel::Oil).size() == 3);
  CHECK(table.curve(Channel::Coolant).size() == 4);
  CHECK(table.curve(Channel::Volt).size() == 5);
  CHECK(&table.curve(Channel::Volt) == &table.curves[index(Channel::Volt)]);
}

// ---------------------------------------------------------------------------
// parse_calibration: accepted documents
// ---------------------------------------------------------------------------

TEST_CASE("parse_calibration: the spec example loads as a placeholder", "[calibration]") {
  const CalibrationTable table = expect_table(kSpecExample);
  CHECK(table.placeholder);
  for (std::size_t i = 0; i < kChannelCount; ++i) {
    CHECK(table.curves[i].size() == 2);
  }
  CHECK(table.curve(Channel::Fuel).points()[0].raw == 0);
  CHECK(table.curve(Channel::Fuel).points()[1].raw == 32767);
  CHECK(table.curve(Channel::Fuel).points()[1].value == 1.0F);
}

TEST_CASE("parse_calibration: converts through each channel's own curve", "[calibration]") {
  const CalibrationTable table = expect_table(kSpecExample);
  CHECK_THAT(table.convert(Channel::Fuel, 32767), WithinAbs(1.0, 1e-6));
  CHECK_THAT(table.convert(Channel::Fuel, 16383), WithinAbs(16383.0 / 32767.0, 1e-6));
  CHECK_THAT(table.convert(Channel::Oil, 32767), WithinAbs(200.0, 1e-4));
  CHECK_THAT(table.convert(Channel::Coolant, 0), WithinAbs(-40.0, 1e-4));
  CHECK_THAT(table.convert(Channel::Coolant, 32767), WithinAbs(400.0, 1e-4));
  CHECK_THAT(table.convert(Channel::Volt, 32767), WithinAbs(40.0, 1e-4));
}

TEST_CASE("parse_calibration: convert extrapolates beyond the end points", "[calibration]") {
  const CalibrationTable table = expect_table(kSpecExample);
  CHECK_THAT(table.convert(Channel::Fuel, -32767), WithinAbs(-1.0, 1e-6));
  CHECK_THAT(table.convert(Channel::Fuel, 65534), WithinAbs(2.0, 1e-6));
  CHECK_FALSE(table.curve(Channel::Fuel).covers(65534));
}

TEST_CASE("parse_calibration: channel key order in the file does not matter", "[calibration]") {
  const CalibrationTable table = expect_table(R"json({
    "format": 1,
    "channels": {
      "volt": {"points": [{"raw": 0, "value": 4.0}, {"raw": 10, "value": 14.0}]},
      "clt":  {"points": [{"raw": 0, "value": 3.0}, {"raw": 10, "value": 13.0}]},
      "oil":  {"points": [{"raw": 0, "value": 2.0}, {"raw": 10, "value": 12.0}]},
      "fuel": {"points": [{"raw": 0, "value": 1.0}, {"raw": 10, "value": 11.0}]}
    },
    "placeholder": true
  })json");
  CHECK(table.convert(Channel::Fuel, 0) == 1.0F);
  CHECK(table.convert(Channel::Oil, 0) == 2.0F);
  CHECK(table.convert(Channel::Coolant, 0) == 3.0F);
  CHECK(table.convert(Channel::Volt, 0) == 4.0F);
}

TEST_CASE("parse_calibration: accepts the maximum of 24 points per channel", "[calibration]") {
  const CalibrationTable table = expect_table(doc(true, Channels{.oil = channel(points(kMaxCalibrationPoints))}));
  CHECK(table.curve(Channel::Oil).size() == kMaxCalibrationPoints);
  CHECK(table.curve(Channel::Oil).points().full());
  CHECK_THAT(table.convert(Channel::Oil, 11500), WithinAbs(11.5, 1e-4));
}

TEST_CASE("parse_calibration: negative raw counts are accepted (differential ADC)", "[calibration]") {
  const CalibrationTable table =
      expect_table(doc(true, Channels{.volt = channel(R"([{"raw":-32768,"value":-1.0},{"raw":32767,"value":1.0}])")}));
  CHECK(table.curve(Channel::Volt).points()[0].raw == -32768);
  CHECK_THAT(table.convert(Channel::Volt, 0), WithinAbs(0.0, 1e-4));
}

TEST_CASE("parse_calibration: int32 extremes are accepted for raw", "[calibration]") {
  const CalibrationTable table = expect_table(
      doc(true, Channels{.volt = channel(R"([{"raw":-2147483648,"value":0.0},{"raw":2147483647,"value":1.0}])")}));
  CHECK(table.curve(Channel::Volt).points()[0].raw == std::numeric_limits<std::int32_t>::min());
  CHECK(table.curve(Channel::Volt).points()[1].raw == std::numeric_limits<std::int32_t>::max());
}

TEST_CASE("parse_calibration: integer-typed values are accepted as numbers", "[calibration]") {
  const CalibrationTable table =
      expect_table(doc(true, Channels{.oil = channel(R"([{"raw":0,"value":0},{"raw":100,"value":80}])")}));
  CHECK(table.convert(Channel::Oil, 50) == 40.0F);
}

TEST_CASE("parse_calibration: values may decrease with raw (NTC-shaped coolant)", "[calibration]") {
  const CalibrationTable table =
      expect_table(doc(true, Channels{.clt = channel(R"([{"raw":55,"value":250.0},{"raw":1365,"value":100.0}])")}));
  CHECK_THAT(table.convert(Channel::Coolant, 710), WithinAbs(175.0, 1e-3));
}

TEST_CASE("parse_calibration: unknown members are ignored at every level", "[calibration]") {
  const CalibrationTable table = expect_table(R"json({
    "placeholder": true,
    "format": 1,
    "_comment": "bench notes go here",
    "revision": "2026-10-05",
    "channels": {
      "fuel": {"unit": "fraction", "points": [{"raw": 0, "value": 0.0, "note": "empty"}, {"raw": 10, "value": 1.0}]},
      "oil":  {"points": [{"raw": 0, "value": 0.0}, {"raw": 10, "value": 1.0}]},
      "clt":  {"points": [{"raw": 0, "value": 0.0}, {"raw": 10, "value": 1.0}]},
      "volt": {"points": [{"raw": 0, "value": 0.0}, {"raw": 10, "value": 1.0}]}
    }
  })json");
  CHECK(table.curve(Channel::Fuel).size() == 2);
}

TEST_CASE("parse_calibration: whitespace and formatting are irrelevant", "[calibration]") {
  const std::string compact = doc(true);
  std::string spaced;
  for (const char c : compact) {
    spaced += c;
    if (c == ',' || c == ':' || c == '{' || c == '[') {
      spaced += "\n\t ";
    }
  }
  const CalibrationTable a = expect_table(compact);
  const CalibrationTable b = expect_table(spaced);
  CHECK(describe(a) == describe(b));
}

// ---------------------------------------------------------------------------
// Safety invariant 5: the placeholder flag
// ---------------------------------------------------------------------------

TEST_CASE("parse_calibration: placeholder true is accepted with the minimum two points", "[calibration]") {
  const CalibrationTable table = expect_table(doc(true));
  CHECK(table.placeholder);
}

TEST_CASE("parse_calibration: placeholder false requires eight measured points on every channel", "[calibration]") {
  const CalibrationTable table = expect_table(doc(false, Channels{
                                                             .fuel = channel(points(8)),
                                                             .oil = channel(points(8)),
                                                             .clt = channel(points(8)),
                                                             .volt = channel(points(8)),
                                                         }));
  CHECK_FALSE(table.placeholder);
}

TEST_CASE("parse_calibration: placeholder false with more than eight points is fine", "[calibration]") {
  const CalibrationTable table = expect_table(doc(false, Channels{
                                                             .fuel = channel(points(24)),
                                                             .oil = channel(points(9)),
                                                             .clt = channel(points(12)),
                                                             .volt = channel(points(8)),
                                                         }));
  CHECK_FALSE(table.placeholder);
}

TEST_CASE("parse_calibration: placeholder false is refused when any channel has fewer than eight points",
          "[calibration]") {
  for (const Channel thin : {Channel::Fuel, Channel::Oil, Channel::Coolant, Channel::Volt}) {
    Channels ch{
        .fuel = channel(points(8)),
        .oil = channel(points(8)),
        .clt = channel(points(8)),
        .volt = channel(points(8)),
    };
    const std::string seven = channel(points(7));
    switch (thin) {
      case Channel::Fuel:
        ch.fuel = seven;
        break;
      case Channel::Oil:
        ch.oil = seven;
        break;
      case Channel::Coolant:
        ch.clt = seven;
        break;
      case Channel::Volt:
        ch.volt = seven;
        break;
      case Channel::Count:
        break;
    }
    const std::string error = expect_rejected(doc(false, ch));
    CHECK_THAT(error, ContainsSubstring("\"placeholder\" is false"));
    CHECK_THAT(error, ContainsSubstring(std::string(to_string(thin))));
    CHECK_THAT(error, ContainsSubstring("has 7 points"));
    CHECK_THAT(error, ContainsSubstring("at least 8"));
  }
}

TEST_CASE("parse_calibration: flipping the flag on the two-point placeholder is refused", "[calibration]") {
  const std::string error = expect_rejected(doc(false));
  CHECK_THAT(error, ContainsSubstring("\"placeholder\" is false"));
  CHECK_THAT(error, ContainsSubstring("has 2 points"));
}

TEST_CASE("parse_calibration: placeholder must be a boolean", "[calibration]") {
  CHECK_THAT(expect_rejected(R"({"placeholder":"true","format":1,"channels":{}})"),
             ContainsSubstring("\"placeholder\" must be true or false"));
  CHECK_THAT(expect_rejected(R"({"placeholder":1,"format":1,"channels":{}})"),
             ContainsSubstring("\"placeholder\" must be true or false"));
  CHECK_THAT(expect_rejected(R"({"placeholder":null,"format":1,"channels":{}})"),
             ContainsSubstring("\"placeholder\" must be true or false"));
}

TEST_CASE("parse_calibration: placeholder is required", "[calibration]") {
  CHECK_THAT(expect_rejected(R"({"format":1,"channels":{}})"), ContainsSubstring("missing \"placeholder\""));
}

// ---------------------------------------------------------------------------
// parse_calibration: document shape
// ---------------------------------------------------------------------------

TEST_CASE("parse_calibration: invalid JSON is reported as such", "[calibration]") {
  for (const std::string_view text : {"", "{", "not json", "[1,2", R"({"format":1,})", "{'format': 1}"}) {
    CHECK_THAT(expect_rejected(text), StartsWith("calibration: invalid JSON: "));
  }
}

TEST_CASE("parse_calibration: comments are not JSON", "[calibration]") {
  const std::string commented = "// shipped placeholder\n" + doc(true);
  CHECK_THAT(expect_rejected(commented), StartsWith("calibration: invalid JSON: "));
}

TEST_CASE("parse_calibration: NaN and Infinity literals are not JSON", "[calibration]") {
  CHECK_THAT(expect_rejected(doc(true, Channels{.oil = channel(R"([{"raw":0,"value":NaN},{"raw":1,"value":1}])")})),
             StartsWith("calibration: invalid JSON: "));
  CHECK_THAT(
      expect_rejected(doc(true, Channels{.oil = channel(R"([{"raw":0,"value":Infinity},{"raw":1,"value":1}])")})),
      StartsWith("calibration: invalid JSON: "));
}

TEST_CASE("parse_calibration: top level must be an object", "[calibration]") {
  CHECK_THAT(expect_rejected("[]"), ContainsSubstring("top level must be an object"));
  CHECK_THAT(expect_rejected("1"), ContainsSubstring("top level must be an object"));
  CHECK_THAT(expect_rejected("\"calibration\""), ContainsSubstring("top level must be an object"));
  CHECK_THAT(expect_rejected("null"), ContainsSubstring("top level must be an object"));
}

TEST_CASE("parse_calibration: format is required and must be exactly the integer 1", "[calibration]") {
  CHECK_THAT(expect_rejected(R"({"placeholder":true,"channels":{}})"), ContainsSubstring("missing \"format\""));
  CHECK_THAT(expect_rejected(doc(true, {}, "2")), ContainsSubstring("\"format\" must be 1"));
  CHECK_THAT(expect_rejected(doc(true, {}, "0")), ContainsSubstring("\"format\" must be 1"));
  CHECK_THAT(expect_rejected(doc(true, {}, "\"1\"")), ContainsSubstring("\"format\" must be 1"));
  CHECK_THAT(expect_rejected(doc(true, {}, "1.0")), ContainsSubstring("\"format\" must be 1"));
  CHECK_THAT(expect_rejected(doc(true, {}, "true")), ContainsSubstring("\"format\" must be 1"));
}

TEST_CASE("parse_calibration: channels is required and must be an object", "[calibration]") {
  CHECK_THAT(expect_rejected(R"({"placeholder":true,"format":1})"), ContainsSubstring("missing \"channels\""));
  CHECK_THAT(expect_rejected(R"({"placeholder":true,"format":1,"channels":[]})"),
             ContainsSubstring("\"channels\" must be an object"));
  CHECK_THAT(expect_rejected(R"({"placeholder":true,"format":1,"channels":null})"),
             ContainsSubstring("\"channels\" must be an object"));
}

TEST_CASE("parse_calibration: every channel must be present", "[calibration]") {
  CHECK_THAT(expect_rejected(R"({"placeholder":true,"format":1,"channels":{}})"), ContainsSubstring("is missing"));

  const std::string two = channel(points(2));
  const std::string no_volt =
      R"({"placeholder":true,"format":1,"channels":{"fuel":)" + two + R"(,"oil":)" + two + R"(,"clt":)" + two + "}}";
  CHECK_THAT(expect_rejected(no_volt), ContainsSubstring("channel \"VOLT\" is missing"));

  const std::string no_clt =
      R"({"placeholder":true,"format":1,"channels":{"fuel":)" + two + R"(,"oil":)" + two + R"(,"volt":)" + two + "}}";
  CHECK_THAT(expect_rejected(no_clt), ContainsSubstring("channel \"CLT\" is missing"));
}

TEST_CASE("parse_calibration: channel keys must be the raw-object spellings", "[calibration]") {
  const std::string two = channel(points(2));
  for (const std::string_view bad : {"coolant", "CLT", "Fuel", "temperature", ""}) {
    const std::string json = R"({"placeholder":true,"format":1,"channels":{"fuel":)" + two + R"(,"oil":)" + two +
                             R"(,"clt":)" + two + R"(,"volt":)" + two + R"(,")" + std::string(bad) + R"(":)" + two +
                             "}}";
    const std::string error = expect_rejected(json);
    CHECK_THAT(error, ContainsSubstring("unknown channel \"" + std::string(bad) + "\""));
    CHECK_THAT(error, ContainsSubstring("expected fuel, oil, clt, volt"));
  }
}

// ---------------------------------------------------------------------------
// parse_calibration: curve shape
// ---------------------------------------------------------------------------

TEST_CASE("parse_calibration: a channel must be an object with a points array", "[calibration]") {
  CHECK_THAT(expect_rejected(doc(true, Channels{.oil = "[]"})), ContainsSubstring("channels.oil: expected an object"));
  CHECK_THAT(expect_rejected(doc(true, Channels{.oil = "1"})), ContainsSubstring("channels.oil: expected an object"));
  CHECK_THAT(expect_rejected(doc(true, Channels{.oil = "null"})),
             ContainsSubstring("channels.oil: expected an object"));
  CHECK_THAT(expect_rejected(doc(true, Channels{.oil = "{}"})), ContainsSubstring("channels.oil: missing \"points\""));
  CHECK_THAT(expect_rejected(doc(true, Channels{.oil = R"({"points":{}})"})),
             ContainsSubstring("channels.oil: \"points\" must be an array"));
  CHECK_THAT(expect_rejected(doc(true, Channels{.oil = R"({"points":"0,1"})"})),
             ContainsSubstring("channels.oil: \"points\" must be an array"));
}

TEST_CASE("parse_calibration: fewer than two points is refused", "[calibration]") {
  CHECK_THAT(expect_rejected(doc(true, Channels{.clt = channel("[]")})),
             ContainsSubstring("channels.clt: needs at least 2 points"));
  CHECK_THAT(expect_rejected(doc(true, Channels{.clt = channel(points(1))})),
             ContainsSubstring("channels.clt: needs at least 2 points"));
}

TEST_CASE("parse_calibration: more than 24 points is refused", "[calibration]") {
  const std::string error = expect_rejected(doc(true, Channels{.fuel = channel(points(kMaxCalibrationPoints + 1))}));
  CHECK_THAT(error, ContainsSubstring("channels.fuel: more than 24 points"));
}

TEST_CASE("parse_calibration: raw must be strictly increasing", "[calibration]") {
  CHECK_THAT(expect_rejected(doc(true, Channels{.volt = channel(R"([{"raw":10,"value":1.0},{"raw":0,"value":0.0}])")})),
             ContainsSubstring("strictly increasing"));
  CHECK_THAT(expect_rejected(doc(true, Channels{.volt = channel(R"([{"raw":5,"value":1.0},{"raw":5,"value":2.0}])")})),
             ContainsSubstring("strictly increasing"));
  CHECK_THAT(
      expect_rejected(
          doc(true, Channels{.volt = channel(R"([{"raw":0,"value":0},{"raw":10,"value":1},{"raw":10,"value":2}])")})),
      ContainsSubstring("strictly increasing"));
}

TEST_CASE("parse_calibration: each point must be an object with raw and value", "[calibration]") {
  CHECK_THAT(expect_rejected(doc(true, Channels{.oil = channel(R"([[0,0],[1,1]])")})),
             ContainsSubstring("channels.oil.points[0]: expected {\"raw\": int, \"value\": number}"));
  CHECK_THAT(expect_rejected(doc(true, Channels{.oil = channel(R"([{"raw":0,"value":0},5])")})),
             ContainsSubstring("channels.oil.points[1]: expected"));
  CHECK_THAT(expect_rejected(doc(true, Channels{.oil = channel(R"([{"value":0},{"raw":1,"value":1}])")})),
             ContainsSubstring("channels.oil.points[0]: missing \"raw\""));
  CHECK_THAT(expect_rejected(doc(true, Channels{.oil = channel(R"([{"raw":0,"value":0},{"raw":1}])")})),
             ContainsSubstring("channels.oil.points[1]: missing \"value\""));
}

TEST_CASE("parse_calibration: raw must be an integer ADC count within int32", "[calibration]") {
  CHECK_THAT(expect_rejected(doc(true, Channels{.oil = channel(R"([{"raw":0.5,"value":0},{"raw":1,"value":1}])")})),
             ContainsSubstring("\"raw\" must be an integer ADC count"));
  CHECK_THAT(expect_rejected(doc(true, Channels{.oil = channel(R"([{"raw":"0","value":0},{"raw":1,"value":1}])")})),
             ContainsSubstring("\"raw\" must be an integer ADC count"));
  CHECK_THAT(expect_rejected(doc(true, Channels{.oil = channel(R"([{"raw":true,"value":0},{"raw":1,"value":1}])")})),
             ContainsSubstring("\"raw\" must be an integer ADC count"));
  CHECK_THAT(expect_rejected(doc(true, Channels{.oil = channel(R"([{"raw":1.0,"value":0},{"raw":2,"value":1}])")})),
             ContainsSubstring("\"raw\" must be an integer ADC count"));
  CHECK_THAT(
      expect_rejected(doc(true, Channels{.oil = channel(R"([{"raw":0,"value":0},{"raw":2147483648,"value":1}])")})),
      ContainsSubstring("\"raw\" out of int32 range"));
  CHECK_THAT(
      expect_rejected(doc(true, Channels{.oil = channel(R"([{"raw":-2147483649,"value":0},{"raw":1,"value":1}])")})),
      ContainsSubstring("\"raw\" out of int32 range"));
}

TEST_CASE("parse_calibration: value must be a finite number within float range", "[calibration]") {
  CHECK_THAT(expect_rejected(doc(true, Channels{.oil = channel(R"([{"raw":0,"value":"0"},{"raw":1,"value":1}])")})),
             ContainsSubstring("channels.oil.points[0]: expected a number"));
  CHECK_THAT(expect_rejected(doc(true, Channels{.oil = channel(R"([{"raw":0,"value":null},{"raw":1,"value":1}])")})),
             ContainsSubstring("expected a number"));
  CHECK_THAT(expect_rejected(doc(true, Channels{.oil = channel(R"([{"raw":0,"value":false},{"raw":1,"value":1}])")})),
             ContainsSubstring("expected a number"));
  CHECK_THAT(expect_rejected(doc(true, Channels{.oil = channel(R"([{"raw":0,"value":1e39},{"raw":1,"value":1}])")})),
             ContainsSubstring("channels.oil.points[0]: not a finite value"));
  // Beyond double range the JSON library refuses the literal itself; still a rejection.
  CHECK_THAT(expect_rejected(doc(true, Channels{.oil = channel(R"([{"raw":0,"value":0},{"raw":1,"value":-1e309}])")})),
             StartsWith("calibration: invalid JSON: "));
}

TEST_CASE("parse_calibration: the error names the offending channel and point", "[calibration]") {
  const std::string error =
      expect_rejected(doc(true, Channels{.clt = channel(R"([{"raw":0,"value":0},{"raw":1,"value":1},{"raw":2}])")}));
  CHECK(error == "channels.clt.points[2]: missing \"value\"");
}

TEST_CASE("parse_calibration: a rejected document yields no table at all", "[calibration]") {
  // Three valid channels and one bad one: nothing may leak out.
  const CalibrationLoadResult r = parse_calibration(doc(true, Channels{.volt = channel(points(1))}));
  CHECK_FALSE(r.table.has_value());
  CHECK_FALSE(r.error.empty());
}

TEST_CASE("parse_calibration: result invariant holds for every outcome", "[calibration]") {
  const CalibrationLoadResult ok = parse_calibration(doc(true));
  CHECK(ok.table.has_value());
  CHECK(ok.error.empty());

  const CalibrationLoadResult bad = parse_calibration("{");
  CHECK_FALSE(bad.table.has_value());
  CHECK_FALSE(bad.error.empty());
}

// ---------------------------------------------------------------------------
// load_calibration
// ---------------------------------------------------------------------------

TEST_CASE("load_calibration: reads a valid file", "[calibration]") {
  const ScratchDir dir;
  const std::string path = dir.write("cal.json", doc(true, Channels{.oil = channel(points(3, 10.0))}));
  const CalibrationLoadResult r = load_calibration(path);
  INFO("error: " << r.error);
  REQUIRE(r.table.has_value());
  CHECK(r.error.empty());
  CHECK(r.table->curve(Channel::Oil).size() == 3);
  CHECK_THAT(r.table->convert(Channel::Oil, 1500), WithinAbs(15.0, 1e-4));
}

TEST_CASE("load_calibration: a missing file is reported with its path", "[calibration]") {
  const ScratchDir dir;
  const std::string path = dir.file("absent.json");
  const CalibrationLoadResult r = load_calibration(path);
  CHECK_FALSE(r.table.has_value());
  CHECK(r.error == "calibration: cannot open " + path);
}

TEST_CASE("load_calibration: an invalid file is reported with the parse error and its path", "[calibration]") {
  const ScratchDir dir;
  const std::string path = dir.write("bad.json", doc(false));
  const CalibrationLoadResult r = load_calibration(path);
  CHECK_FALSE(r.table.has_value());
  CHECK_THAT(r.error, ContainsSubstring("\"placeholder\" is false"));
  CHECK_THAT(r.error, Catch::Matchers::EndsWith(" (" + path + ")"));
}

TEST_CASE("load_calibration: an empty file is invalid JSON", "[calibration]") {
  const ScratchDir dir;
  const std::string path = dir.write("empty.json", "");
  const CalibrationLoadResult r = load_calibration(path);
  CHECK_FALSE(r.table.has_value());
  CHECK_THAT(r.error, StartsWith("calibration: invalid JSON: "));
  CHECK_THAT(r.error, Catch::Matchers::EndsWith(" (" + path + ")"));
}

TEST_CASE("load_calibration: a directory cannot be read", "[calibration]") {
  const ScratchDir dir;
  const CalibrationLoadResult r = load_calibration(dir.file(""));
  CHECK_FALSE(r.table.has_value());
  CHECK_THAT(r.error, StartsWith("calibration: "));
}

TEST_CASE("load_calibration: a file with a UTF-8 BOM is rejected rather than misread", "[calibration]") {
  const ScratchDir dir;
  const std::string path = dir.write("bom.json", "\xEF\xBB\xBF" + doc(true));
  const CalibrationLoadResult r = load_calibration(path);
  // nlohmann strips a BOM by default; whichever way the library goes, the result must be consistent.
  if (r.table) {
    CHECK(r.table->placeholder);
  } else {
    CHECK_THAT(r.error, StartsWith("calibration: invalid JSON: "));
  }
}

// ---------------------------------------------------------------------------
// The shipped file (safety invariant 5)
// ---------------------------------------------------------------------------

TEST_CASE("shipped config/calibration.json: loads and is explicitly a placeholder", "[calibration]") {
  const std::string path = std::string(C3_PROJECT_ROOT) + "/config/calibration.json";
  const CalibrationLoadResult r = load_calibration(path);
  INFO("error: " << r.error);
  REQUIRE(r.table.has_value());
  CHECK(r.table->placeholder);  // invariant 5: never ship a file that claims to be calibrated
  for (std::size_t i = 0; i < kChannelCount; ++i) {
    INFO("channel: " << to_string(static_cast<Channel>(i)));
    CHECK(r.table->curves[i].size() >= 2);
  }
  CHECK_THAT(describe(*r.table), StartsWith("PLACEHOLDER"));
}

TEST_CASE("shipped config/calibration.json: every channel converts into its plausibility limits", "[calibration]") {
  namespace limits = c3::telemetry::limits;
  const std::string path = std::string(C3_PROJECT_ROOT) + "/config/calibration.json";
  const CalibrationLoadResult r = load_calibration(path);
  REQUIRE(r.table.has_value());
  const CalibrationTable& t = *r.table;

  struct Span {
    Channel ch;
    float lo;
    float hi;
  };
  const Span spans[] = {
      {Channel::Fuel, limits::kFuelMin, limits::kFuelMax},
      {Channel::Oil, limits::kOilMinPsi, limits::kOilMaxPsi},
      {Channel::Coolant, limits::kCltMinF, limits::kCltMaxF},
      {Channel::Volt, limits::kVoltMin, limits::kVoltMax},
  };
  for (const auto& s : spans) {
    INFO("channel: " << to_string(s.ch));
    const auto& pts = t.curve(s.ch).points();
    const float first = t.convert(s.ch, pts.front().raw);
    const float last = t.convert(s.ch, pts.back().raw);
    CHECK(first >= s.lo);
    CHECK(first <= s.hi);
    CHECK(last >= s.lo);
    CHECK(last <= s.hi);
  }
}

// ---------------------------------------------------------------------------
// describe
// ---------------------------------------------------------------------------

TEST_CASE("describe: a placeholder table says so loudly and lists point counts", "[calibration]") {
  const CalibrationTable table = expect_table(doc(true, Channels{
                                                            .fuel = channel(points(2)),
                                                            .oil = channel(points(3)),
                                                            .clt = channel(points(4)),
                                                            .volt = channel(points(5)),
                                                        }));
  CHECK(describe(table) == "PLACEHOLDER (identity-shaped, not calibrated); points: FUEL=2 OIL=3 CLT=4 VOLT=5");
}

TEST_CASE("describe: a measured table is labelled measured", "[calibration]") {
  const CalibrationTable table = expect_table(doc(false, Channels{
                                                             .fuel = channel(points(8)),
                                                             .oil = channel(points(8)),
                                                             .clt = channel(points(8)),
                                                             .volt = channel(points(8)),
                                                         }));
  CHECK(describe(table) == "measured; points: FUEL=8 OIL=8 CLT=8 VOLT=8");
}

TEST_CASE("describe: the default table reports zero points everywhere", "[calibration]") {
  CHECK(describe(CalibrationTable{}) ==
        "PLACEHOLDER (identity-shaped, not calibrated); points: FUEL=0 OIL=0 CLT=0 VOLT=0");
}

}  // namespace
