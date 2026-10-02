#include "utils/unique_fd.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <type_traits>
#include <utility>

using c3::utils::UniqueFd;

namespace {

/// Opens a throwaway descriptor the test fully owns.
[[nodiscard]] int open_scratch_fd() {
  const int fd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
  REQUIRE(fd >= 0);
  return fd;
}

/// True when the kernel still has `fd` open in this process.
[[nodiscard]] bool is_open(int fd) noexcept {
  return ::fcntl(fd, F_GETFD) != -1;
}

/// True when `fd` has been closed (or never existed).
[[nodiscard]] bool is_closed(int fd) noexcept {
  errno = 0;
  return ::fcntl(fd, F_GETFD) == -1 && errno == EBADF;
}

// ---------------------------------------------------------------------------
// Type properties
// ---------------------------------------------------------------------------

TEST_CASE("UniqueFd: move-only with noexcept moves", "[unique_fd]") {
  STATIC_CHECK_FALSE(std::is_copy_constructible_v<UniqueFd>);
  STATIC_CHECK_FALSE(std::is_copy_assignable_v<UniqueFd>);
  STATIC_CHECK(std::is_nothrow_move_constructible_v<UniqueFd>);
  STATIC_CHECK(std::is_nothrow_move_assignable_v<UniqueFd>);
  STATIC_CHECK(std::is_nothrow_default_constructible_v<UniqueFd>);
}

TEST_CASE("UniqueFd: constructor from int is explicit", "[unique_fd]") {
  STATIC_CHECK_FALSE(std::is_convertible_v<int, UniqueFd>);
  STATIC_CHECK(std::is_constructible_v<UniqueFd, int>);
}

TEST_CASE("UniqueFd: conversion to bool is explicit", "[unique_fd]") {
  STATIC_CHECK_FALSE(std::is_convertible_v<UniqueFd, bool>);
  STATIC_CHECK(std::is_constructible_v<bool, UniqueFd>);
}

// ---------------------------------------------------------------------------
// Construction and observers
// ---------------------------------------------------------------------------

TEST_CASE("UniqueFd: default-constructed holds -1 and is invalid", "[unique_fd]") {
  const UniqueFd fd;
  CHECK(fd.get() == -1);
  CHECK_FALSE(fd.valid());
  CHECK_FALSE(static_cast<bool>(fd));
}

TEST_CASE("UniqueFd: constructed from an open descriptor is valid and reports it", "[unique_fd]") {
  const int raw = open_scratch_fd();
  const UniqueFd fd(raw);
  CHECK(fd.get() == raw);
  CHECK(fd.valid());
  CHECK(static_cast<bool>(fd));
  CHECK(is_open(raw));
}

TEST_CASE("UniqueFd: constructed from a negative descriptor is invalid", "[unique_fd]") {
  const UniqueFd minus_one(-1);
  const UniqueFd minus_five(-5);
  CHECK_FALSE(minus_one.valid());
  CHECK_FALSE(minus_five.valid());
  CHECK(minus_five.get() == -5);
}

// ---------------------------------------------------------------------------
// Destruction
// ---------------------------------------------------------------------------

TEST_CASE("UniqueFd: destructor closes the descriptor", "[unique_fd]") {
  const int raw = open_scratch_fd();
  {
    const UniqueFd fd(raw);
    REQUIRE(is_open(raw));
  }
  CHECK(is_closed(raw));
}

TEST_CASE("UniqueFd: destructor of an invalid handle does nothing harmful", "[unique_fd]") {
  const int unrelated = open_scratch_fd();
  {
    const UniqueFd fd;
    const UniqueFd negative(-7);
  }
  CHECK(is_open(unrelated));
  static_cast<void>(::close(unrelated));
}

// ---------------------------------------------------------------------------
// release / reset
// ---------------------------------------------------------------------------

TEST_CASE("release: hands back the descriptor and leaves the handle invalid without closing", "[unique_fd]") {
  const int raw = open_scratch_fd();
  UniqueFd fd(raw);

  const int released = fd.release();
  CHECK(released == raw);
  CHECK(fd.get() == -1);
  CHECK_FALSE(fd.valid());
  CHECK(is_open(raw));

  static_cast<void>(::close(raw));
}

TEST_CASE("release: on an invalid handle returns -1", "[unique_fd]") {
  UniqueFd fd;
  CHECK(fd.release() == -1);
  CHECK_FALSE(fd.valid());
}

TEST_CASE("release: a released descriptor is not closed when the handle is destroyed", "[unique_fd]") {
  const int raw = open_scratch_fd();
  {
    UniqueFd fd(raw);
    CHECK(fd.release() == raw);
  }
  CHECK(is_open(raw));
  static_cast<void>(::close(raw));
}

TEST_CASE("reset: with no argument closes the descriptor and invalidates the handle", "[unique_fd]") {
  const int raw = open_scratch_fd();
  UniqueFd fd(raw);

  fd.reset();
  CHECK(is_closed(raw));
  CHECK(fd.get() == -1);
  CHECK_FALSE(fd.valid());
}

TEST_CASE("reset: with a new descriptor closes the old one and adopts the new", "[unique_fd]") {
  const int first = open_scratch_fd();
  const int second = open_scratch_fd();
  UniqueFd fd(first);

  fd.reset(second);
  CHECK(is_closed(first));
  CHECK(is_open(second));
  CHECK(fd.get() == second);
  CHECK(fd.valid());
}

TEST_CASE("reset: on an invalid handle adopts the new descriptor", "[unique_fd]") {
  const int raw = open_scratch_fd();
  UniqueFd fd;

  fd.reset(raw);
  CHECK(fd.get() == raw);
  CHECK(fd.valid());
  CHECK(is_open(raw));
}

TEST_CASE("reset: on an invalid handle with no argument is a no-op", "[unique_fd]") {
  UniqueFd fd;
  fd.reset();
  CHECK_FALSE(fd.valid());
  CHECK(fd.get() == -1);
}

// ---------------------------------------------------------------------------
// Move semantics
// ---------------------------------------------------------------------------

TEST_CASE("move constructor: transfers ownership and leaves the source invalid", "[unique_fd]") {
  const int raw = open_scratch_fd();
  UniqueFd source(raw);

  const UniqueFd target(std::move(source));
  CHECK(target.get() == raw);
  CHECK(target.valid());
  CHECK(source.get() == -1);  // NOLINT(bugprone-use-after-move): moved-from state is the subject
  CHECK_FALSE(source.valid());
  CHECK(is_open(raw));
}

TEST_CASE("move constructor: the descriptor is closed exactly once, by the new owner", "[unique_fd]") {
  const int raw = open_scratch_fd();
  {
    UniqueFd source(raw);
    {
      const UniqueFd target(std::move(source));
      REQUIRE(is_open(raw));
    }
    CHECK(is_closed(raw));
    // `source` is destroyed here while invalid; it must not touch `raw` again.
  }
  CHECK(is_closed(raw));
}

TEST_CASE("move assignment: closes the target's old descriptor and adopts the source's", "[unique_fd]") {
  const int old_fd = open_scratch_fd();
  const int new_fd = open_scratch_fd();
  UniqueFd target(old_fd);
  UniqueFd source(new_fd);

  target = std::move(source);
  CHECK(is_closed(old_fd));
  CHECK(is_open(new_fd));
  CHECK(target.get() == new_fd);
  CHECK(source.get() == -1);  // NOLINT(bugprone-use-after-move): moved-from state is the subject
  CHECK_FALSE(source.valid());
}

TEST_CASE("move assignment: from an invalid source closes the target and leaves it invalid", "[unique_fd]") {
  const int raw = open_scratch_fd();
  UniqueFd target(raw);
  UniqueFd source;

  target = std::move(source);
  CHECK(is_closed(raw));
  CHECK_FALSE(target.valid());
  CHECK(target.get() == -1);
}

TEST_CASE("move assignment: into an invalid target simply adopts the descriptor", "[unique_fd]") {
  const int raw = open_scratch_fd();
  UniqueFd target;
  UniqueFd source(raw);

  target = std::move(source);
  CHECK(target.get() == raw);
  CHECK(is_open(raw));
  CHECK_FALSE(source.valid());  // NOLINT(bugprone-use-after-move): moved-from state is the subject
}

TEST_CASE("move assignment: self-assignment keeps the descriptor open and owned", "[unique_fd]") {
  const int raw = open_scratch_fd();
  UniqueFd fd(raw);
  UniqueFd& alias = fd;  // through a reference so the compiler cannot flag an obvious self-move

  fd = std::move(alias);
  CHECK(fd.get() == raw);
  CHECK(fd.valid());
  CHECK(is_open(raw));
}

TEST_CASE("move assignment: chained moves close the descriptor only when the last owner dies", "[unique_fd]") {
  const int raw = open_scratch_fd();
  {
    UniqueFd a(raw);
    UniqueFd b;
    UniqueFd c;
    b = std::move(a);
    c = std::move(b);
    CHECK(c.get() == raw);
    CHECK_FALSE(a.valid());  // NOLINT(bugprone-use-after-move): moved-from state is the subject
    CHECK_FALSE(b.valid());  // NOLINT(bugprone-use-after-move): moved-from state is the subject
    CHECK(is_open(raw));
  }
  CHECK(is_closed(raw));
}

}  // namespace
