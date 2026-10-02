#include "utils/fixed_vector.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <iterator>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

struct Reading {
  int channel = 0;
  double value = 0.0;

  [[nodiscard]] constexpr bool operator==(const Reading&) const noexcept = default;
};

// ---------------------------------------------------------------------------
// Type properties
// ---------------------------------------------------------------------------

TEST_CASE("FixedVector: kCapacity mirrors the template argument", "[fixed_vector]") {
  STATIC_CHECK(c3::utils::FixedVector<int, 4>::kCapacity == 4);
  STATIC_CHECK(c3::utils::FixedVector<Reading, 1>::kCapacity == 1);
}

TEST_CASE("FixedVector: trivially copyable when the element type is", "[fixed_vector]") {
  STATIC_CHECK(std::is_trivially_copyable_v<c3::utils::FixedVector<int, 4>>);
  STATIC_CHECK(std::is_trivially_copyable_v<c3::utils::FixedVector<Reading, 4>>);
  STATIC_CHECK(std::is_trivially_copyable_v<c3::utils::FixedVector<std::string_view, 4>>);
}

TEST_CASE("FixedVector: mutating and querying operations are noexcept", "[fixed_vector]") {
  c3::utils::FixedVector<int, 4> v;
  const auto& cv = v;
  STATIC_CHECK(noexcept(v.push_back(1)));
  STATIC_CHECK(noexcept(v.clear()));
  STATIC_CHECK(noexcept(cv.size()));
  STATIC_CHECK(noexcept(cv.empty()));
  STATIC_CHECK(noexcept(cv.full()));
  STATIC_CHECK(noexcept(cv[0]));
  STATIC_CHECK(noexcept(cv.front()));
  STATIC_CHECK(noexcept(cv.back()));
  STATIC_CHECK(noexcept(cv.begin()));
  STATIC_CHECK(noexcept(cv.end()));
}

TEST_CASE("FixedVector: usable in constant expressions", "[fixed_vector]") {
  constexpr auto v = [] {
    c3::utils::FixedVector<int, 3> out;
    static_cast<void>(out.push_back(10));
    static_cast<void>(out.push_back(20));
    return out;
  }();
  STATIC_CHECK(v.size() == 2);
  STATIC_CHECK(v[0] == 10);
  STATIC_CHECK(v[1] == 20);
  STATIC_CHECK(v.front() == 10);
  STATIC_CHECK(v.back() == 20);
  STATIC_CHECK_FALSE(v.full());
}

// ---------------------------------------------------------------------------
// Fresh container
// ---------------------------------------------------------------------------

TEST_CASE("FixedVector: fresh container is empty, not full, with size 0", "[fixed_vector]") {
  const c3::utils::FixedVector<int, 4> v;
  CHECK(v.size() == 0);
  CHECK(v.empty());
  CHECK_FALSE(v.full());
}

TEST_CASE("FixedVector: fresh container has an empty iteration range", "[fixed_vector]") {
  const c3::utils::FixedVector<int, 4> v;
  CHECK(v.begin() == v.end());
  CHECK(std::distance(v.begin(), v.end()) == 0);
}

// ---------------------------------------------------------------------------
// push_back
// ---------------------------------------------------------------------------

TEST_CASE("push_back: returns true and grows size by one", "[fixed_vector]") {
  c3::utils::FixedVector<int, 4> v;
  CHECK(v.push_back(7));
  CHECK(v.size() == 1);
  CHECK_FALSE(v.empty());
  CHECK_FALSE(v.full());
  CHECK(v[0] == 7);
}

TEST_CASE("push_back: preserves insertion order", "[fixed_vector]") {
  c3::utils::FixedVector<int, 4> v;
  CHECK(v.push_back(1));
  CHECK(v.push_back(2));
  CHECK(v.push_back(3));
  CHECK(v.size() == 3);
  CHECK(v[0] == 1);
  CHECK(v[1] == 2);
  CHECK(v[2] == 3);
}

TEST_CASE("push_back: reaching capacity reports full", "[fixed_vector]") {
  c3::utils::FixedVector<int, 3> v;
  CHECK(v.push_back(1));
  CHECK(v.push_back(2));
  CHECK_FALSE(v.full());
  CHECK(v.push_back(3));
  CHECK(v.full());
  CHECK(v.size() == 3);
}

TEST_CASE("push_back: returns false and leaves the container unchanged when full", "[fixed_vector]") {
  c3::utils::FixedVector<int, 2> v;
  CHECK(v.push_back(1));
  CHECK(v.push_back(2));

  CHECK_FALSE(v.push_back(99));
  CHECK_FALSE(v.push_back(100));

  CHECK(v.size() == 2);
  CHECK(v.full());
  CHECK(v[0] == 1);
  CHECK(v[1] == 2);
  CHECK(v.back() == 2);
}

TEST_CASE("push_back: capacity of one fills on the first push", "[fixed_vector]") {
  c3::utils::FixedVector<int, 1> v;
  CHECK(v.push_back(42));
  CHECK(v.full());
  CHECK_FALSE(v.push_back(43));
  CHECK(v.size() == 1);
  CHECK(v.front() == 42);
  CHECK(v.back() == 42);
}

TEST_CASE("push_back: copies the value rather than aliasing the argument", "[fixed_vector]") {
  c3::utils::FixedVector<int, 2> v;
  int source = 5;
  CHECK(v.push_back(source));
  source = 6;
  CHECK(v[0] == 5);
}

TEST_CASE("push_back: stores aggregate element types", "[fixed_vector]") {
  c3::utils::FixedVector<Reading, 4> v;
  CHECK(v.push_back(Reading{.channel = 0, .value = 41.5}));
  CHECK(v.push_back(Reading{.channel = 1, .value = 12.25}));
  CHECK(v.size() == 2);
  CHECK(v[0] == Reading{.channel = 0, .value = 41.5});
  CHECK(v[1] == Reading{.channel = 1, .value = 12.25});
  CHECK(v.back().channel == 1);
}

TEST_CASE("push_back: stores string_view elements without copying the text", "[fixed_vector]") {
  constexpr std::string_view kFuel = "fuel";
  constexpr std::string_view kOil = "oil";
  c3::utils::FixedVector<std::string_view, 4> v;
  CHECK(v.push_back(kFuel));
  CHECK(v.push_back(kOil));
  CHECK(v[0] == "fuel");
  CHECK(v[1] == "oil");
  CHECK(v[0].data() == kFuel.data());
}

// ---------------------------------------------------------------------------
// front / back / operator[]
// ---------------------------------------------------------------------------

TEST_CASE("front/back: track the first and most recently pushed elements", "[fixed_vector]") {
  c3::utils::FixedVector<int, 4> v;
  CHECK(v.push_back(10));
  CHECK(v.front() == 10);
  CHECK(v.back() == 10);

  CHECK(v.push_back(20));
  CHECK(v.front() == 10);
  CHECK(v.back() == 20);

  CHECK(v.push_back(30));
  CHECK(v.front() == 10);
  CHECK(v.back() == 30);
}

TEST_CASE("operator[]: non-const access writes through to the element", "[fixed_vector]") {
  c3::utils::FixedVector<int, 4> v;
  CHECK(v.push_back(1));
  CHECK(v.push_back(2));
  v[1] = 200;
  CHECK(v[1] == 200);
  CHECK(v.back() == 200);
  CHECK(v.size() == 2);
}

TEST_CASE("operator[]: const access returns a const reference", "[fixed_vector]") {
  c3::utils::FixedVector<int, 4> v;
  CHECK(v.push_back(1));
  const auto& cv = v;
  STATIC_CHECK(std::is_same_v<decltype(cv[0]), const int&>);
  STATIC_CHECK(std::is_same_v<decltype(v[0]), int&>);
  CHECK(cv[0] == 1);
}

// ---------------------------------------------------------------------------
// clear
// ---------------------------------------------------------------------------

TEST_CASE("clear: empties the container and makes it pushable again", "[fixed_vector]") {
  c3::utils::FixedVector<int, 2> v;
  CHECK(v.push_back(1));
  CHECK(v.push_back(2));
  CHECK(v.full());

  v.clear();
  CHECK(v.size() == 0);
  CHECK(v.empty());
  CHECK_FALSE(v.full());
  CHECK(v.begin() == v.end());

  CHECK(v.push_back(9));
  CHECK(v.size() == 1);
  CHECK(v[0] == 9);
  CHECK(v.front() == 9);
  CHECK(v.back() == 9);
}

TEST_CASE("clear: on an empty container is a no-op", "[fixed_vector]") {
  c3::utils::FixedVector<int, 2> v;
  v.clear();
  CHECK(v.empty());
  CHECK(v.size() == 0);
}

// ---------------------------------------------------------------------------
// Iteration
// ---------------------------------------------------------------------------

TEST_CASE("begin/end: range spans exactly the pushed elements, not the capacity", "[fixed_vector]") {
  c3::utils::FixedVector<int, 8> v;
  CHECK(v.push_back(3));
  CHECK(v.push_back(1));
  CHECK(v.push_back(4));

  CHECK(std::distance(v.begin(), v.end()) == 3);
  CHECK(v.end() == v.begin() + 3);

  std::vector<int> seen;
  for (const int x : v) {
    seen.push_back(x);
  }
  CHECK(seen == std::vector<int>{3, 1, 4});
}

TEST_CASE("begin/end: const iteration yields const elements in order", "[fixed_vector]") {
  c3::utils::FixedVector<int, 4> v;
  CHECK(v.push_back(5));
  CHECK(v.push_back(6));
  const auto& cv = v;
  STATIC_CHECK(std::is_same_v<decltype(cv.begin()), const int*>);

  std::vector<int> seen;
  for (const auto& x : cv) {
    seen.push_back(x);
  }
  CHECK(seen == std::vector<int>{5, 6});
}

TEST_CASE("begin/end: mutable iteration writes through to the elements", "[fixed_vector]") {
  c3::utils::FixedVector<int, 4> v;
  CHECK(v.push_back(1));
  CHECK(v.push_back(2));
  CHECK(v.push_back(3));

  for (auto& x : v) {
    x *= 10;
  }
  CHECK(v[0] == 10);
  CHECK(v[1] == 20);
  CHECK(v[2] == 30);
  CHECK(v.size() == 3);
}

TEST_CASE("begin/end: full container iterates every slot", "[fixed_vector]") {
  c3::utils::FixedVector<int, 3> v;
  CHECK(v.push_back(1));
  CHECK(v.push_back(2));
  CHECK(v.push_back(3));
  CHECK(v.full());
  CHECK(std::distance(v.begin(), v.end()) == 3);
}

// ---------------------------------------------------------------------------
// Copy semantics
// ---------------------------------------------------------------------------

TEST_CASE("copy: a copied container is independent of the original", "[fixed_vector]") {
  c3::utils::FixedVector<int, 4> original;
  CHECK(original.push_back(1));
  CHECK(original.push_back(2));

  auto copy = original;
  CHECK(copy.size() == 2);
  CHECK(copy[0] == 1);
  CHECK(copy[1] == 2);

  copy[0] = 100;
  CHECK(copy.push_back(3));
  CHECK(original[0] == 1);
  CHECK(original.size() == 2);
  CHECK(copy.size() == 3);
}

TEST_CASE("copy: assignment replaces size and contents", "[fixed_vector]") {
  c3::utils::FixedVector<int, 4> a;
  c3::utils::FixedVector<int, 4> b;
  CHECK(a.push_back(1));
  CHECK(a.push_back(2));
  CHECK(a.push_back(3));
  CHECK(b.push_back(9));

  a = b;
  CHECK(a.size() == 1);
  CHECK(a[0] == 9);
  CHECK(std::distance(a.begin(), a.end()) == 1);
}

}  // namespace
