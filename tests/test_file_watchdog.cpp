#include "platform/file_watchdog.hpp"
#include "platform/watchdog.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

using c3::platform::FileWatchdog;
using c3::platform::NullWatchdog;
using c3::platform::Watchdog;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;

namespace {

/// A fresh scratch directory, removed when the test case ends. The watchdog device path is a
/// config value so that tests can point it at an ordinary file; that is what happens here.
class ScratchDir {
public:
  ScratchDir() {
    static unsigned counter = 0;
    path_ = std::filesystem::temp_directory_path() /
            ("c3console_watchdog_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
    std::filesystem::remove_all(path_);
    REQUIRE(std::filesystem::create_directory(path_));
  }
  ~ScratchDir() { std::filesystem::remove_all(path_); }
  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;
  ScratchDir(ScratchDir&&) = delete;
  ScratchDir& operator=(ScratchDir&&) = delete;

  /// Creates an empty regular file, as the device node already exists on the target.
  [[nodiscard]] std::string device(std::string_view name = "watchdog") const {
    const auto path = (path_ / name).string();
    std::ofstream out(path, std::ios::binary);
    REQUIRE(out.is_open());
    return path;
  }

  [[nodiscard]] std::string file(std::string_view name) const { return (path_ / name).string(); }
  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
  std::filesystem::path path_;
};

[[nodiscard]] std::string read_all(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.is_open());
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

[[nodiscard]] std::unique_ptr<FileWatchdog> open_or_fail(const std::string& path) {
  auto result = FileWatchdog::open(path);
  INFO("error: " << result.error);
  REQUIRE(result.watchdog != nullptr);
  REQUIRE(result.error.empty());
  return std::move(result.watchdog);
}

/// The descriptor number the next open() will return: POSIX hands out the lowest free one.
[[nodiscard]] int next_free_fd() {
  const int probe = ::open("/dev/null", O_RDONLY);
  REQUIRE(probe >= 0);
  REQUIRE(::close(probe) == 0);
  return probe;
}

[[nodiscard]] bool fd_is_open(int fd) noexcept {
  return ::fcntl(fd, F_GETFD) != -1;
}

/// Ignores SIGPIPE for one scope so a write to a reader-less FIFO fails with EPIPE instead of
/// killing the test process.
class IgnoreSigpipe {
public:
  IgnoreSigpipe() : previous_(std::signal(SIGPIPE, SIG_IGN)) {}
  ~IgnoreSigpipe() { static_cast<void>(std::signal(SIGPIPE, previous_)); }
  IgnoreSigpipe(const IgnoreSigpipe&) = delete;
  IgnoreSigpipe& operator=(const IgnoreSigpipe&) = delete;
  IgnoreSigpipe(IgnoreSigpipe&&) = delete;
  IgnoreSigpipe& operator=(IgnoreSigpipe&&) = delete;

private:
  void (*previous_)(int);
};

// ---------------------------------------------------------------------------
// Interface
// ---------------------------------------------------------------------------

TEST_CASE("FileWatchdog: is a final, non-copyable Watchdog with a noexcept kick", "[file_watchdog]") {
  STATIC_CHECK(std::is_base_of_v<Watchdog, FileWatchdog>);
  STATIC_CHECK(std::is_final_v<FileWatchdog>);
  STATIC_CHECK_FALSE(std::is_copy_constructible_v<FileWatchdog>);
  STATIC_CHECK_FALSE(std::is_move_constructible_v<FileWatchdog>);
  STATIC_CHECK_FALSE(std::is_default_constructible_v<FileWatchdog>);  // only open() creates one
  STATIC_CHECK(noexcept(std::declval<FileWatchdog&>().kick()));
  STATIC_CHECK(noexcept(std::declval<FileWatchdog&>().allow_clean_exit()));
  STATIC_CHECK(std::has_virtual_destructor_v<Watchdog>);
}

TEST_CASE("NullWatchdog: kick is a no-op usable through the base interface", "[file_watchdog]") {
  NullWatchdog null;
  Watchdog& wd = null;
  for (int i = 0; i < 100; ++i) {
    wd.kick();
  }
  STATIC_CHECK(noexcept(null.kick()));
  SUCCEED("NullWatchdog kicked 100 times without side effects");
}

// ---------------------------------------------------------------------------
// open
// ---------------------------------------------------------------------------

TEST_CASE("open: an existing writable device opens with zeroed counters", "[file_watchdog]") {
  const ScratchDir dir;
  const auto wd = open_or_fail(dir.device());
  CHECK(wd->kicks() == 0);
  CHECK(wd->failures() == 0);
}

TEST_CASE("open: does not write anything by itself", "[file_watchdog]") {
  const ScratchDir dir;
  const std::string path = dir.device();
  {
    const auto wd = open_or_fail(path);
    CHECK(read_all(path).empty());
  }
  CHECK(read_all(path).empty());  // nor on a non-clean destruction
}

TEST_CASE("open: the device is opened write-only, close-on-exec, and closed on destruction", "[file_watchdog]") {
  const ScratchDir dir;
  const std::string path = dir.device();
  const int expected_fd = next_free_fd();
  {
    const auto wd = open_or_fail(path);
    REQUIRE(fd_is_open(expected_fd));
    const int flags = ::fcntl(expected_fd, F_GETFL);
    REQUIRE(flags >= 0);
    CHECK((flags & O_ACCMODE) == O_WRONLY);  // nothing on the host ever reads the watchdog
    const int fd_flags = ::fcntl(expected_fd, F_GETFD);
    REQUIRE(fd_flags >= 0);
    CHECK((fd_flags & FD_CLOEXEC) != 0);  // a child process must not inherit the device
    wd->kick();
    CHECK(read_all(path) == "k");  // confirms expected_fd really is the watchdog's descriptor
  }
  CHECK_FALSE(fd_is_open(expected_fd));
}

TEST_CASE("open: a missing device is an error naming the path and the reason", "[file_watchdog]") {
  const ScratchDir dir;
  const std::string path = dir.file("absent");
  const auto result = FileWatchdog::open(path);
  CHECK(result.watchdog == nullptr);
  CHECK_THAT(result.error, StartsWith("watchdog: cannot open " + path + ": "));
  CHECK_THAT(result.error, ContainsSubstring("No such file"));
}

TEST_CASE("open: does not create the device when it is missing", "[file_watchdog]") {
  // The real node is created by the kernel driver; a stray regular file would silently
  // disable the hardware watchdog while every kick appeared to succeed.
  const ScratchDir dir;
  const std::string path = dir.file("absent");
  CHECK(FileWatchdog::open(path).watchdog == nullptr);
  CHECK_FALSE(std::filesystem::exists(path));
}

TEST_CASE("open: a directory is an error", "[file_watchdog]") {
  const ScratchDir dir;
  const auto result = FileWatchdog::open(dir.path().string());
  CHECK(result.watchdog == nullptr);
  CHECK_THAT(result.error, StartsWith("watchdog: cannot open "));
  CHECK_THAT(result.error, ContainsSubstring("directory"));
}

TEST_CASE("open: a device the process may not write is an error", "[file_watchdog]") {
  if (::geteuid() == 0) {
    SKIP("root bypasses file permissions");
  }
  const ScratchDir dir;
  const std::string path = dir.device("readonly");
  REQUIRE(::chmod(path.c_str(), 0444) == 0);
  const auto result = FileWatchdog::open(path);
  CHECK(result.watchdog == nullptr);
  CHECK_THAT(result.error, StartsWith("watchdog: cannot open " + path + ": "));
  CHECK_THAT(result.error, ContainsSubstring("ermission"));
}

TEST_CASE("open: the real target device path is reported verbatim when unavailable", "[file_watchdog]") {
  // On the desk /dev/watchdog is normally absent; on the target it exists. Either way the
  // error must name the configured path so the log line is actionable.
  const auto result = FileWatchdog::open("/dev/watchdog-c3console-does-not-exist");
  REQUIRE(result.watchdog == nullptr);
  CHECK_THAT(result.error, StartsWith("watchdog: cannot open /dev/watchdog-c3console-does-not-exist: "));
}

// ---------------------------------------------------------------------------
// kick
// ---------------------------------------------------------------------------

TEST_CASE("kick: each kick writes exactly one byte and is counted", "[file_watchdog]") {
  const ScratchDir dir;
  const std::string path = dir.device();
  const auto wd = open_or_fail(path);

  wd->kick();
  CHECK(wd->kicks() == 1);
  CHECK(read_all(path) == "k");

  wd->kick();
  wd->kick();
  CHECK(wd->kicks() == 3);
  CHECK(wd->failures() == 0);
  CHECK(read_all(path) == "kkk");
}

TEST_CASE("kick: writes land on the device immediately, without buffering", "[file_watchdog]") {
  // The driver's timer only sees bytes that reach it; a buffered kick is no kick.
  const ScratchDir dir;
  const std::string path = dir.device();
  const auto wd = open_or_fail(path);
  for (unsigned i = 1; i <= 10; ++i) {
    wd->kick();
    CHECK(std::filesystem::file_size(path) == i);
  }
}

TEST_CASE("kick: a long run of kicks never fails and never writes the magic character", "[file_watchdog]") {
  const ScratchDir dir;
  const std::string path = dir.device();
  {
    const auto wd = open_or_fail(path);
    for (int i = 0; i < 1000; ++i) {
      wd->kick();
    }
    CHECK(wd->kicks() == 1000);
    CHECK(wd->failures() == 0);
  }
  const std::string content = read_all(path);
  CHECK(content.size() == 1000);
  CHECK(content.find('V') == std::string::npos);
  CHECK(content.find_first_not_of('k') == std::string::npos);
}

TEST_CASE("kick: through the Watchdog interface behaves identically", "[file_watchdog]") {
  const ScratchDir dir;
  const std::string path = dir.device();
  auto owned = open_or_fail(path);
  Watchdog& wd = *owned;
  wd.kick();
  wd.kick();
  CHECK(owned->kicks() == 2);
  CHECK(read_all(path) == "kk");
}

TEST_CASE("kick: a failed write is counted as a failure, not a kick", "[file_watchdog]") {
  const ScratchDir dir;
  const std::string fifo = dir.file("fifo");
  REQUIRE(::mkfifo(fifo.c_str(), 0600) == 0);

  // Hold a reader open so the write-only open() succeeds, then drop it.
  int reader = ::open(fifo.c_str(), O_RDONLY | O_NONBLOCK);
  REQUIRE(reader >= 0);
  const auto wd = open_or_fail(fifo);

  wd->kick();  // reader present: succeeds
  CHECK(wd->kicks() == 1);
  CHECK(wd->failures() == 0);

  REQUIRE(::close(reader) == 0);
  reader = -1;
  {
    const IgnoreSigpipe guard;
    wd->kick();  // no reader: EPIPE
    wd->kick();
  }
  CHECK(wd->kicks() == 1);
  CHECK(wd->failures() == 2);
}

TEST_CASE("kick: failures do not stop later kicks from being attempted", "[file_watchdog]") {
  const ScratchDir dir;
  const std::string fifo = dir.file("fifo");
  REQUIRE(::mkfifo(fifo.c_str(), 0600) == 0);
  int reader = ::open(fifo.c_str(), O_RDONLY | O_NONBLOCK);
  REQUIRE(reader >= 0);
  const auto wd = open_or_fail(fifo);
  REQUIRE(::close(reader) == 0);
  {
    const IgnoreSigpipe guard;
    wd->kick();
    REQUIRE(wd->failures() == 1);
  }
  // A reader comes back: the next kick goes through.
  reader = ::open(fifo.c_str(), O_RDONLY | O_NONBLOCK);
  REQUIRE(reader >= 0);
  wd->kick();
  CHECK(wd->kicks() == 1);
  CHECK(wd->failures() == 1);
  char byte = 0;
  CHECK(::read(reader, &byte, 1) == 1);
  CHECK(byte == 'k');
  REQUIRE(::close(reader) == 0);
}

// ---------------------------------------------------------------------------
// Magic close (spec §5.6: a deliberate stop must not reboot the box; a crash must)
// ---------------------------------------------------------------------------

TEST_CASE("destructor: without allow_clean_exit nothing is written, so the hardware would reboot", "[file_watchdog]") {
  const ScratchDir dir;
  const std::string path = dir.device();
  {
    const auto wd = open_or_fail(path);
    wd->kick();
    wd->kick();
  }
  CHECK(read_all(path) == "kk");
}

TEST_CASE("destructor: with allow_clean_exit the magic 'V' is the last byte written", "[file_watchdog]") {
  const ScratchDir dir;
  const std::string path = dir.device();
  {
    const auto wd = open_or_fail(path);
    wd->kick();
    wd->kick();
    wd->allow_clean_exit();
  }
  CHECK(read_all(path) == "kkV");
}

TEST_CASE("allow_clean_exit: arms the magic close but writes nothing until destruction", "[file_watchdog]") {
  // Writing 'V' early would let a hang that happens after it slip past the hardware.
  const ScratchDir dir;
  const std::string path = dir.device();
  {
    const auto wd = open_or_fail(path);
    wd->allow_clean_exit();
    CHECK(read_all(path).empty());
    wd->kick();
    CHECK(read_all(path) == "k");  // kicks after arming are still plain kicks
    wd->kick();
    CHECK(read_all(path) == "kk");
  }
  CHECK(read_all(path) == "kkV");
}

TEST_CASE("allow_clean_exit: is idempotent and does not count as a kick", "[file_watchdog]") {
  const ScratchDir dir;
  const std::string path = dir.device();
  {
    const auto wd = open_or_fail(path);
    wd->allow_clean_exit();
    wd->allow_clean_exit();
    wd->allow_clean_exit();
    CHECK(wd->kicks() == 0);
  }
  CHECK(read_all(path) == "V");  // exactly one magic character
}

TEST_CASE("destructor: a clean exit with no kicks at all still sends the magic close", "[file_watchdog]") {
  const ScratchDir dir;
  const std::string path = dir.device();
  {
    const auto wd = open_or_fail(path);
    wd->allow_clean_exit();
  }
  CHECK(read_all(path) == "V");
}

TEST_CASE("destructor: two watchdogs on different devices do not interfere", "[file_watchdog]") {
  const ScratchDir dir;
  const std::string a_path = dir.device("a");
  const std::string b_path = dir.device("b");
  {
    const auto a = open_or_fail(a_path);
    const auto b = open_or_fail(b_path);
    a->kick();
    b->kick();
    b->kick();
    b->allow_clean_exit();
  }
  CHECK(read_all(a_path) == "k");
  CHECK(read_all(b_path) == "kkV");
}

}  // namespace
