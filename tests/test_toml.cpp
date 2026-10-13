// toml.cpp — the console.toml subset reader. Grammar, error reporting and the typed lookups.
#include "app/toml.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <unistd.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using c3::toml::Array;
using c3::toml::Document;
using c3::toml::get_bool;
using c3::toml::get_float;
using c3::toml::get_int;
using c3::toml::get_int_array;
using c3::toml::get_string;
using c3::toml::parse;
using c3::toml::parse_file;
using c3::toml::ParseError;
using c3::toml::Value;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

namespace {

/// Unique temporary directory removed on destruction.
class ScratchDir {
public:
  ScratchDir() {
    static int counter = 0;
    path_ = std::filesystem::temp_directory_path() /
            ("c3_toml_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
    std::filesystem::create_directories(path_);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;
  ScratchDir(ScratchDir&&) = delete;
  ScratchDir& operator=(ScratchDir&&) = delete;

  [[nodiscard]] std::string file(std::string_view name, std::string_view content) const {
    const std::filesystem::path p = path_ / name;
    std::ofstream(p, std::ios::binary) << content;
    return p.string();
  }
  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
  std::filesystem::path path_;
};

/// The value at [section] key; the test fails if it is absent.
[[nodiscard]] const Value& at(const Document& doc, std::string_view section, std::string_view key) {
  const auto sec = doc.find(section);
  REQUIRE(sec != doc.end());
  const auto it = sec->second.find(key);
  REQUIRE(it != sec->second.end());
  return it->second;
}

[[nodiscard]] std::int64_t as_int(const Value& v) {
  REQUIRE(v.is_int());
  return std::get<std::int64_t>(v.data);
}
[[nodiscard]] double as_float(const Value& v) {
  REQUIRE(v.is_float());
  return std::get<double>(v.data);
}
[[nodiscard]] const std::string& as_string(const Value& v) {
  REQUIRE(v.is_string());
  return std::get<std::string>(v.data);
}
[[nodiscard]] bool as_bool(const Value& v) {
  REQUIRE(v.is_bool());
  return std::get<bool>(v.data);
}

/// Parses `text` expecting a ParseError; returns it for inspection.
[[nodiscard]] ParseError expect_error(std::string_view text) {
  try {
    static_cast<void>(parse(text));
  } catch (const ParseError& e) {
    return e;
  }
  FAIL("expected a ParseError for: " << text);
  return ParseError(0, "");
}

constexpr std::string_view kSample = R"(# console.toml excerpt
root = 1

[display]
backend = "drm"       # trailing comment
width = 1600
dpi = 264.0
rotation = 0

[telemetry]
source = "mock"
stall_timeout_ms = 1_000
baud = 0x1C200

[carplay]
usb_product_ids = [0x1520, 0x1521]
enabled = true
)";

// ---------------------------------------------------------------------------
// documents, sections and keys
// ---------------------------------------------------------------------------

TEST_CASE("toml: a representative document parses into sections with typed values", "[toml]") {
  const Document doc = parse(kSample);
  CHECK(doc.size() == 4);  // "", display, telemetry, carplay
  CHECK(as_int(at(doc, "", "root")) == 1);
  CHECK(as_string(at(doc, "display", "backend")) == "drm");
  CHECK(as_int(at(doc, "display", "width")) == 1600);
  CHECK_THAT(as_float(at(doc, "display", "dpi")), WithinAbs(264.0, 1e-9));
  CHECK(as_int(at(doc, "display", "rotation")) == 0);
  CHECK(as_string(at(doc, "telemetry", "source")) == "mock");
  CHECK(as_int(at(doc, "telemetry", "stall_timeout_ms")) == 1000);
  CHECK(as_int(at(doc, "telemetry", "baud")) == 115200);
  CHECK(as_bool(at(doc, "carplay", "enabled")));
  const Value& pids = at(doc, "carplay", "usb_product_ids");
  REQUIRE(pids.is_array());
  const Array& items = std::get<Array>(pids.data);
  REQUIRE(items.size() == 2);
  CHECK(as_int(items[0]) == 0x1520);
  CHECK(as_int(items[1]) == 0x1521);
}

TEST_CASE("toml: an empty or comment-only document is an empty document", "[toml]") {
  CHECK(parse("").empty());
  CHECK(parse("\n\n").empty());
  CHECK(parse("# just a comment\n   # another").empty());
}

TEST_CASE("toml: keys before any section header live in the root table", "[toml]") {
  const Document doc = parse("a = 1\nb = \"x\"");
  REQUIRE(doc.size() == 1);
  CHECK(doc.begin()->first.empty());
  CHECK(as_int(at(doc, "", "a")) == 1);
  CHECK(as_string(at(doc, "", "b")) == "x");
}

TEST_CASE("toml: a section with no keys is still a section", "[toml]") {
  const Document doc = parse("[empty]\n[other]\nk = 1");
  REQUIRE(doc.count("empty") == 1);
  CHECK(doc.find("empty")->second.empty());
  CHECK(as_int(at(doc, "other", "k")) == 1);
}

TEST_CASE("toml: section names may be padded and dotted but are never nested", "[toml]") {
  const Document doc = parse("[ padded ]\nk = 1\n[a.b]\nk = 2");
  CHECK(as_int(at(doc, "padded", "k")) == 1);
  CHECK(as_int(at(doc, "a.b", "k")) == 2);
  CHECK(doc.count("a") == 0);
}

TEST_CASE("toml: reopening a section merges its keys (lenient), but a repeated key is an error", "[toml]") {
  const Document doc = parse("[s]\na = 1\n[t]\n[s]\nb = 2");
  CHECK(as_int(at(doc, "s", "a")) == 1);
  CHECK(as_int(at(doc, "s", "b")) == 2);

  const ParseError e = expect_error("[s]\na = 1\n[s]\na = 2");
  CHECK(e.line() == 4);
  CHECK(e.message() == "duplicate key 'a'");
}

TEST_CASE("toml: duplicate keys in one section are rejected on the second line", "[toml]") {
  const ParseError e = expect_error("a = 1\na = 1");
  CHECK(e.line() == 2);
  CHECK(e.message() == "duplicate key 'a'");
}

TEST_CASE("toml: bare keys may use letters, digits, underscore and dash; anything else is rejected", "[toml]") {
  const Document doc = parse("a-b_c1 = 1");
  CHECK(as_int(at(doc, "", "a-b_c1")) == 1);
  CHECK(expect_error("\"quoted\" = 1").message() == "only bare keys are supported");
  CHECK(expect_error("two words = 1").message() == "only bare keys are supported");
  CHECK(expect_error("a.b = 1").message() == "only bare keys are supported");
  CHECK(expect_error("= 1").message() == "empty key");
  CHECK(expect_error("novalue").message() == "expected key = value");
  CHECK(expect_error("a =").message() == "missing value");
  CHECK(expect_error("a =   # only a comment").message() == "missing value");
}

TEST_CASE("toml: section header errors name the problem", "[toml]") {
  CHECK(expect_error("[]").message() == "malformed section header");
  CHECK(expect_error("[a").message() == "malformed section header");
  CHECK(expect_error("[abc").message() == "malformed section header");
  CHECK(expect_error("[[table]]").message() == "arrays of tables are not supported");
  CHECK(expect_error("[a b]").message() == "bad section name");
  CHECK(expect_error("[a/b]").message() == "bad section name");
}

TEST_CASE("toml: whitespace around keys, equals and values is ignored and CRLF endings are accepted", "[toml]") {
  const Document doc = parse("  a\t=\t 1  \r\n[s]\r\n  b = \"x\"  \r\n");
  CHECK(as_int(at(doc, "", "a")) == 1);
  CHECK(as_string(at(doc, "s", "b")) == "x");
}

TEST_CASE("toml: a # inside a string is not a comment", "[toml]") {
  const Document doc = parse("s = \"a # b\"  # real comment\nt = \"x\\\"#y\"");
  CHECK(as_string(at(doc, "", "s")) == "a # b");
  CHECK(as_string(at(doc, "", "t")) == "x\"#y");
}

// ---------------------------------------------------------------------------
// scalars
// ---------------------------------------------------------------------------

TEST_CASE("toml: strings decode the four supported escapes", "[toml]") {
  const Document doc = parse(R"(a = "plain"
b = "quote \" inside"
c = "back\\slash"
d = "line\nbreak"
e = "tab\there"
f = "")");
  CHECK(as_string(at(doc, "", "a")) == "plain");
  CHECK(as_string(at(doc, "", "b")) == "quote \" inside");
  CHECK(as_string(at(doc, "", "c")) == "back\\slash");
  CHECK(as_string(at(doc, "", "d")) == "line\nbreak");
  CHECK(as_string(at(doc, "", "e")) == "tab\there");
  CHECK(as_string(at(doc, "", "f")).empty());
}

TEST_CASE("toml: malformed strings are rejected", "[toml]") {
  CHECK(expect_error("a = \"open").message() == "unterminated string");
  CHECK(expect_error("a = \"").message() == "unterminated string");
  CHECK(expect_error("a = \"x\\\"").message() == "dangling escape");
  CHECK(expect_error("a = \"a\"b\"").message() == "unexpected quote inside string");
  CHECK(expect_error("a = \"bad \\q escape\"").message() == "unsupported escape \\q");
  CHECK(expect_error("a = \"u\\u0041\"").message() == "unsupported escape \\u");
}

TEST_CASE("toml: integers accept sign, underscores and 0x hex", "[toml]") {
  const Document doc = parse("a = 42\nb = -7\nc = +3\nd = 1_000_000\ne = 0xFF\nf = 0Xff_ff\ng = -0x10\nh = 0");
  CHECK(as_int(at(doc, "", "a")) == 42);
  CHECK(as_int(at(doc, "", "b")) == -7);
  CHECK(as_int(at(doc, "", "c")) == 3);
  CHECK(as_int(at(doc, "", "d")) == 1000000);
  CHECK(as_int(at(doc, "", "e")) == 255);
  CHECK(as_int(at(doc, "", "f")) == 65535);
  CHECK(as_int(at(doc, "", "g")) == -16);  // lenient: TOML forbids signed hex
  CHECK(as_int(at(doc, "", "h")) == 0);
}

TEST_CASE("toml: integers that do not fit or are not numbers are rejected", "[toml]") {
  CHECK(as_int(at(parse("a = 9223372036854775807"), "", "a")) == INT64_MAX);
  CHECK(expect_error("a = 9223372036854775808").message() == "bad integer '9223372036854775808'");
  CHECK(expect_error("a = 12abc").message() == "bad integer '12abc'");
  CHECK(expect_error("a = 0x").message() == "bad integer '0x'");
  CHECK(expect_error("a = 0xG1").message() == "bad integer '0xG1'");
  CHECK(expect_error("a = -").message() == "bad integer '-'");
}

TEST_CASE("toml: a bare word is neither a boolean nor a number", "[toml]") {
  CHECK(expect_error("a = yes").message() == "bad value 'yes'");
  CHECK(expect_error("a = True").message() == "bad value 'True'");
  CHECK(expect_error("a = inf").message() == "bad value 'inf'");
  CHECK(expect_error("a = nan").message() == "bad value 'nan'");
  CHECK(expect_error("a = unquoted string").message() == "bad value 'unquoted string'");
  CHECK(expect_error("a = [1, nope]").message() == "bad value 'nope'");
}

TEST_CASE("toml: floats are anything with a point or exponent, and stay distinct from integers", "[toml]") {
  const Document doc = parse("a = 1.5\nb = -2.25\nc = 1e3\nd = 6.02E23\ne = 2_000.5\nf = 1.0");
  CHECK_THAT(as_float(at(doc, "", "a")), WithinAbs(1.5, 1e-12));
  CHECK_THAT(as_float(at(doc, "", "b")), WithinAbs(-2.25, 1e-12));
  CHECK_THAT(as_float(at(doc, "", "c")), WithinAbs(1000.0, 1e-9));
  CHECK_THAT(as_float(at(doc, "", "d")), WithinAbs(6.02e23, 1e9));
  CHECK_THAT(as_float(at(doc, "", "e")), WithinAbs(2000.5, 1e-9));
  CHECK(at(doc, "", "f").is_float());
  CHECK_FALSE(at(doc, "", "f").is_int());
  CHECK(at(parse("g = 1"), "", "g").is_int());
}

TEST_CASE("toml: malformed floats are rejected; a missing digit beside the point is tolerated", "[toml]") {
  CHECK(expect_error("a = 1.5.5").message() == "bad float '1.5.5'");
  CHECK(expect_error("a = 1e").message() == "bad float '1e'");
  CHECK(expect_error("a = 1.5x").message() == "bad float '1.5x'");
  CHECK(expect_error("a = .").message() == "bad float '.'");
  CHECK_THAT(as_float(at(parse("a = 1."), "", "a")), WithinAbs(1.0, 1e-12));  // lenient
  CHECK_THAT(as_float(at(parse("a = .5"), "", "a")), WithinAbs(0.5, 1e-12));  // lenient
}

TEST_CASE("toml: booleans are exactly true and false", "[toml]") {
  const Document doc = parse("a = true\nb = false");
  CHECK(as_bool(at(doc, "", "a")));
  CHECK_FALSE(as_bool(at(doc, "", "b")));
  CHECK(at(doc, "", "a").is_bool());
  CHECK_FALSE(at(doc, "", "a").is_int());
}

// ---------------------------------------------------------------------------
// arrays
// ---------------------------------------------------------------------------

TEST_CASE("toml: arrays hold scalars, allow a trailing comma and may be empty", "[toml]") {
  const Document doc = parse(R"(a = [1, 2, 3]
b = []
c = [1, 2,]
d = ["x,y", "z"]
e = [1, "two", true, 4.0]
f = [ 0x1520 , 0x1521 ])");
  const auto items = [&](std::string_view key) -> const Array& {
    const Value& v = at(doc, "", key);
    REQUIRE(v.is_array());
    return std::get<Array>(v.data);
  };
  REQUIRE(items("a").size() == 3);
  CHECK(as_int(items("a")[2]) == 3);
  CHECK(items("b").empty());
  CHECK(items("c").size() == 2);
  REQUIRE(items("d").size() == 2);
  CHECK(as_string(items("d")[0]) == "x,y");  // the comma inside the string does not split
  CHECK(as_string(items("d")[1]) == "z");
  REQUIRE(items("e").size() == 4);  // the parser allows mixed types; the typed getters police them
  CHECK(as_int(items("e")[0]) == 1);
  CHECK(as_string(items("e")[1]) == "two");
  CHECK(as_bool(items("e")[2]));
  CHECK(as_float(items("e")[3]) == 4.0);
  REQUIRE(items("f").size() == 2);
  CHECK(as_int(items("f")[1]) == 0x1521);
}

TEST_CASE("toml: malformed arrays are rejected", "[toml]") {
  CHECK(expect_error("a = [1, 2").message() == "unterminated array (arrays must be on one line)");
  CHECK(expect_error("a = [1,\n2]").message() == "unterminated array (arrays must be on one line)");
  CHECK(expect_error("a = [1] x").message() == "unterminated array (arrays must be on one line)");
  CHECK(expect_error("a = [1,,2]").message() == "empty array element");
  CHECK(expect_error("a = [,1]").message() == "empty array element");
  CHECK(expect_error("a = [1, [2]]").message() == "nested arrays are not supported");
  CHECK(expect_error("a = [1, \"open]").message() == "unterminated string");
  CHECK(expect_error("a = [\"a\", \"b]").message() == "unterminated string");
}

// ---------------------------------------------------------------------------
// error reporting
// ---------------------------------------------------------------------------

TEST_CASE("toml: ParseError carries the 1-based line and formats what() from it", "[toml]") {
  const ParseError e = expect_error("ok = 1\n\n# comment\nbad =\n");
  CHECK(e.line() == 4);
  CHECK(e.message() == "missing value");
  CHECK(e.file().empty());
  CHECK(std::string(e.what()) == "line 4: missing value");
}

TEST_CASE("toml: the first error wins and nothing after it is reported", "[toml]") {
  const ParseError e = expect_error("a = \"open\nb = [1,,2]");
  CHECK(e.line() == 1);
  CHECK(e.message() == "unterminated string");
}

TEST_CASE("toml: parse_file reads a file and prefixes errors with its path", "[toml]") {
  const ScratchDir dir;
  const std::string good = dir.file("good.toml", "[s]\nk = 3\n");
  CHECK(as_int(at(parse_file(good), "s", "k")) == 3);

  const std::string bad = dir.file("bad.toml", "[s]\nk = \n");
  try {
    static_cast<void>(parse_file(bad));
    FAIL("expected ParseError");
  } catch (const ParseError& e) {
    CHECK(e.line() == 2);
    CHECK(e.message() == "missing value");
    CHECK(e.file() == bad);
    CHECK(std::string(e.what()) == bad + ":line 2: missing value");
  }
}

TEST_CASE("toml: parse_file on a missing file throws runtime_error naming the path", "[toml]") {
  const ScratchDir dir;
  const std::string missing = (dir.path() / "absent.toml").string();
  CHECK_THROWS_MATCHES(parse_file(missing), std::runtime_error, Catch::Matchers::Message("cannot open " + missing));
}

TEST_CASE("toml: parse_file refuses a directory instead of reading it as an empty document", "[toml]") {
  const ScratchDir dir;
  const std::string path = dir.path().string();
  CHECK_THROWS_MATCHES(parse_file(path), std::runtime_error,
                       Catch::Matchers::Message("cannot read " + path + ": not a regular file"));
}

TEST_CASE("toml: parse_file accepts an empty file", "[toml]") {
  const ScratchDir dir;
  CHECK(parse_file(dir.file("empty.toml", "")).empty());
}

// ---------------------------------------------------------------------------
// typed lookups
// ---------------------------------------------------------------------------

constexpr std::string_view kLookup = R"(root = 1
[s]
str = "x"
int = 7
flt = 2.5
yes = true
arr = [1, 2]
mixed = [1, "a"]
)";

TEST_CASE("toml: getters return the value when present and the fallback when absent", "[toml]") {
  const Document doc = parse(kLookup);
  CHECK(get_string(doc, "s", "str", "d") == "x");
  CHECK(get_int(doc, "s", "int", -1) == 7);
  CHECK(get_float(doc, "s", "flt", 0.0) == 2.5);
  CHECK(get_bool(doc, "s", "yes", false));
  CHECK(get_int_array(doc, "s", "arr", {}) == std::vector<std::int64_t>{1, 2});
  CHECK(get_int(doc, "", "root", -1) == 1);

  CHECK(get_string(doc, "s", "absent", "d") == "d");
  CHECK(get_int(doc, "s", "absent", -1) == -1);
  CHECK(get_float(doc, "s", "absent", 9.5) == 9.5);
  CHECK(get_bool(doc, "s", "absent", true));
  CHECK(get_int_array(doc, "s", "absent", {4, 5}) == std::vector<std::int64_t>{4, 5});
  CHECK(get_int(doc, "nosuch", "int", -1) == -1);  // absent section, too
}

TEST_CASE("toml: get_float accepts an integer where a float is expected", "[toml]") {
  const Document doc = parse(kLookup);
  CHECK(get_float(doc, "s", "int", 0.0) == 7.0);
}

TEST_CASE("toml: getters throw on a present key of the wrong type, naming section, key and type", "[toml]") {
  const Document doc = parse(kLookup);
  CHECK_THROWS_MATCHES(get_string(doc, "s", "int", ""), std::runtime_error,
                       Catch::Matchers::Message("[s] int: expected string"));
  CHECK_THROWS_MATCHES(get_int(doc, "s", "flt", 0), std::runtime_error,
                       Catch::Matchers::Message("[s] flt: expected integer"));
  CHECK_THROWS_MATCHES(get_int(doc, "s", "str", 0), std::runtime_error,
                       Catch::Matchers::Message("[s] str: expected integer"));
  CHECK_THROWS_MATCHES(get_float(doc, "s", "str", 0.0), std::runtime_error,
                       Catch::Matchers::Message("[s] str: expected number"));
  CHECK_THROWS_MATCHES(get_bool(doc, "s", "int", false), std::runtime_error,
                       Catch::Matchers::Message("[s] int: expected boolean"));
  CHECK_THROWS_MATCHES(get_int_array(doc, "s", "int", {}), std::runtime_error,
                       Catch::Matchers::Message("[s] int: expected array of integers"));
  CHECK_THROWS_MATCHES(get_int_array(doc, "s", "mixed", {}), std::runtime_error,
                       Catch::Matchers::Message("[s] mixed: expected array of integers"));
}

TEST_CASE("toml: lookups accept string_view keys without allocating a std::string", "[toml]") {
  const Document doc = parse(kLookup);
  constexpr std::string_view kSection = "s";
  constexpr std::string_view kKey = "int";
  CHECK(get_int(doc, kSection, kKey, 0) == 7);
}

}  // namespace
