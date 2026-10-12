#include "logging/log.hpp"
#include "platform/display_backend.hpp"
#include "platform/input_source.hpp"
#include "platform/libinput_input_source.hpp"
#include "platform/panel_rotation.hpp"
#include "utils/unique_fd.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

using c3::platform::InputSource;
using c3::platform::LibinputInputSource;
using c3::platform::Point2;
using c3::platform::Rotation;
using c3::platform::Size;
using c3::platform::to_logical;
using c3::platform::TouchEvent;
using c3::platform::TouchPhase;
using c3::utils::UniqueFd;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

CATCH_REGISTER_ENUM(TouchPhase, TouchPhase::Down, TouchPhase::Move, TouchPhase::Up, TouchPhase::Cancel)

namespace {

// ---------------------------------------------------------------------------
// Two tiers. The first needs libinput and udev to initialise, which works with no input
// devices at all (the Bookworm container). The second drives the real libinput path through a
// virtual touchscreen made with uinput; that needs /dev/uinput and a running udev to classify
// the device, so it runs on the CM4 and skips elsewhere. Those cases are tagged [rig].
// ---------------------------------------------------------------------------

constexpr Size kNative{.width = 1280, .height = 800};  // landscape scanout, like the rig panel

void discard_log(void* /*context*/, std::string_view /*line*/) noexcept {}
[[nodiscard]] c3::logging::Sink quiet_sink() noexcept {
  return c3::logging::Sink{.context = nullptr, .emit = &discard_log};
}

[[nodiscard]] bool input_devices_present() {
  std::error_code ec;
  return std::filesystem::is_directory("/dev/input", ec) && !std::filesystem::is_empty("/dev/input", ec);
}

struct Opened {
  std::unique_ptr<c3::logging::ScopedSink> quiet;
  std::unique_ptr<LibinputInputSource> source;
  [[nodiscard]] LibinputInputSource* operator->() const noexcept { return source.get(); }
  [[nodiscard]] LibinputInputSource& operator*() const noexcept { return *source; }
};

/// Creates a source, or skips when this machine has no input stack to attach to.
[[nodiscard]] Opened create_or_skip(const LibinputInputSource::Options& options) {
  Opened opened{.quiet = std::make_unique<c3::logging::ScopedSink>(quiet_sink()), .source = nullptr};
  auto result = LibinputInputSource::create(options);
  if (!result.source) {
    if (!input_devices_present()) {
      SKIP("no input devices on this machine: " << result.error);
    }
    FAIL("LibinputInputSource::create failed with /dev/input present: " << result.error);
  }
  REQUIRE(result.error.empty());
  opened.source = std::move(result.source);
  return opened;
}

[[nodiscard]] Opened create_or_skip() {
  return create_or_skip(LibinputInputSource::Options{.native = kNative, .rotation = Rotation::R0});
}

[[nodiscard]] std::optional<TouchEvent> poll(LibinputInputSource& source) noexcept {
  TouchEvent out;
  return source.poll(out) ? std::optional<TouchEvent>{out} : std::nullopt;
}

/// Polls repeatedly for up to `budget`, returning the first event that arrives.
[[nodiscard]] std::optional<TouchEvent> poll_within(LibinputInputSource& source, std::chrono::milliseconds budget) {
  const auto deadline = std::chrono::steady_clock::now() + budget;
  do {
    if (const auto e = poll(source)) {
      return e;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  } while (std::chrono::steady_clock::now() < deadline);
  return std::nullopt;
}

// ---------------------------------------------------------------------------
// A virtual multitouch digitizer. Raw axes run 0..4095, so a raw value of 2048 lands at exactly
// half the native dimension after libinput's scaling, and 1024 at a quarter.
// ---------------------------------------------------------------------------

class VirtualTouchscreen {
public:
  static constexpr int kRawMax = 4095;

  [[nodiscard]] static bool available() {
    std::error_code ec;
    return ::access("/dev/uinput", W_OK) == 0 && std::filesystem::exists("/run/udev/control", ec);
  }

  VirtualTouchscreen() {
    fd_ = UniqueFd(::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC));
    REQUIRE(fd_.valid());
    set(UI_SET_EVBIT, EV_SYN);
    set(UI_SET_EVBIT, EV_KEY);
    set(UI_SET_EVBIT, EV_ABS);
    set(UI_SET_KEYBIT, BTN_TOUCH);
    set(UI_SET_PROPBIT, INPUT_PROP_DIRECT);
    abs_axis(ABS_X, kRawMax);
    abs_axis(ABS_Y, kRawMax);
    abs_axis(ABS_MT_POSITION_X, kRawMax);
    abs_axis(ABS_MT_POSITION_Y, kRawMax);
    abs_axis(ABS_MT_SLOT, 9);
    abs_axis(ABS_MT_TRACKING_ID, 65535);

    uinput_setup setup{};
    setup.id.bustype = BUS_USB;
    setup.id.vendor = 0x1234;
    setup.id.product = 0x5678;
    setup.id.version = 1;
    std::snprintf(setup.name, sizeof(setup.name), "c3console test touchscreen %d", ::getpid());  // NOLINT
    REQUIRE(::ioctl(fd_.get(), UI_DEV_SETUP, &setup) == 0);
    REQUIRE(::ioctl(fd_.get(), UI_DEV_CREATE) == 0);
  }

  ~VirtualTouchscreen() {
    if (fd_.valid()) {
      static_cast<void>(::ioctl(fd_.get(), UI_DEV_DESTROY));
    }
  }
  VirtualTouchscreen(const VirtualTouchscreen&) = delete;
  VirtualTouchscreen& operator=(const VirtualTouchscreen&) = delete;
  VirtualTouchscreen(VirtualTouchscreen&&) = delete;
  VirtualTouchscreen& operator=(VirtualTouchscreen&&) = delete;

  void down(int slot, int tracking_id, int x, int y) {
    emit(EV_ABS, ABS_MT_SLOT, slot);
    emit(EV_ABS, ABS_MT_TRACKING_ID, tracking_id);
    emit(EV_ABS, ABS_MT_POSITION_X, x);
    emit(EV_ABS, ABS_MT_POSITION_Y, y);
    if (slot == 0) {
      emit(EV_KEY, BTN_TOUCH, 1);
      emit(EV_ABS, ABS_X, x);
      emit(EV_ABS, ABS_Y, y);
    }
    sync();
  }

  void move(int slot, int x, int y) {
    emit(EV_ABS, ABS_MT_SLOT, slot);
    emit(EV_ABS, ABS_MT_POSITION_X, x);
    emit(EV_ABS, ABS_MT_POSITION_Y, y);
    if (slot == 0) {
      emit(EV_ABS, ABS_X, x);
      emit(EV_ABS, ABS_Y, y);
    }
    sync();
  }

  void up(int slot) {
    emit(EV_ABS, ABS_MT_SLOT, slot);
    emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
    if (slot == 0) {
      emit(EV_KEY, BTN_TOUCH, 0);
    }
    sync();
  }

private:
  void set(unsigned long request, int bit) {  // NOLINT(google-runtime-int): ioctl's type
    REQUIRE(::ioctl(fd_.get(), request, bit) == 0);
  }

  void abs_axis(int code, int maximum) {
    uinput_abs_setup axis{};
    axis.code = static_cast<__u16>(code);
    axis.absinfo.minimum = 0;
    axis.absinfo.maximum = maximum;
    REQUIRE(::ioctl(fd_.get(), UI_ABS_SETUP, &axis) == 0);
  }

  void emit(int type, int code, int value) {
    input_event ev{};
    ev.type = static_cast<__u16>(type);
    ev.code = static_cast<__u16>(code);
    ev.value = value;
    REQUIRE(::write(fd_.get(), &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev)));
  }

  void sync() { emit(EV_SYN, SYN_REPORT, 0); }

  UniqueFd fd_;
};

/// Taps repeatedly until libinput has picked the new device up and delivers the Down, so the
/// test does not race udev. Returns the Down event.
[[nodiscard]] TouchEvent first_down(LibinputInputSource& source, VirtualTouchscreen& screen, int x, int y) {
  for (int attempt = 0; attempt < 20; ++attempt) {
    screen.down(0, 1 + attempt, x, y);
    if (const auto e = poll_within(source, std::chrono::milliseconds(250))) {
      REQUIRE(e->phase == TouchPhase::Down);
      return *e;
    }
    screen.up(0);
    static_cast<void>(poll_within(source, std::chrono::milliseconds(50)));
  }
  FAIL("libinput never delivered a touch from the virtual device");
  return {};
}

/// The logical point libinput should report for a raw digitizer coordinate under `r`.
[[nodiscard]] Point2 expected_logical(int raw_x, int raw_y, Rotation r) noexcept {
  const float nx = static_cast<float>(raw_x) * static_cast<float>(kNative.width) / (VirtualTouchscreen::kRawMax + 1);
  const float ny = static_cast<float>(raw_y) * static_cast<float>(kNative.height) / (VirtualTouchscreen::kRawMax + 1);
  return to_logical({.x = nx, .y = ny}, kNative, r);
}

// ---------------------------------------------------------------------------
// Tier 1: no input device required
// ---------------------------------------------------------------------------

TEST_CASE("LibinputInputSource: options default to no rotation on seat0", "[libinput_input_source]") {
  const LibinputInputSource::Options defaults{};
  CHECK(defaults.native == Size{});
  CHECK(defaults.rotation == Rotation::R0);
  CHECK(defaults.seat == "seat0");
}

TEST_CASE("LibinputInputSource: is a final, non-copyable InputSource that never asks to quit",
          "[libinput_input_source]") {
  STATIC_CHECK(std::is_base_of_v<InputSource, LibinputInputSource>);
  STATIC_CHECK(std::is_final_v<LibinputInputSource>);
  STATIC_CHECK_FALSE(std::is_copy_constructible_v<LibinputInputSource>);
  STATIC_CHECK_FALSE(std::is_move_constructible_v<LibinputInputSource>);
  STATIC_CHECK_FALSE(std::is_default_constructible_v<LibinputInputSource>);
  STATIC_CHECK(noexcept(std::declval<LibinputInputSource&>().poll(std::declval<TouchEvent&>())));
  STATIC_CHECK(noexcept(std::declval<const LibinputInputSource&>().quit_requested()));
  STATIC_CHECK(noexcept(std::declval<const LibinputInputSource&>().descriptor()));
}

TEST_CASE("create: a non-positive native size is refused before udev is touched", "[libinput_input_source]") {
  const c3::logging::ScopedSink quiet(quiet_sink());
  for (const Size bad :
       {Size{.width = 0, .height = 800}, Size{.width = 1280, .height = 0}, Size{.width = -1, .height = 800}, Size{}}) {
    INFO("native " << bad.width << "x" << bad.height);
    const auto result = LibinputInputSource::create({.native = bad});
    CHECK(result.source == nullptr);
    CHECK(result.error == "touch: native panel size must be positive");
  }
}

TEST_CASE("create: attaches to the seat and exposes a pollable descriptor", "[libinput_input_source]") {
  const auto source = create_or_skip();
  CHECK(source->descriptor() >= 0);
  CHECK_FALSE(source->quit_requested());
  CHECK(source->dropped() == 0);
}

TEST_CASE("poll: with no touch activity returns nothing, promptly, every time", "[libinput_input_source]") {
  const auto source = create_or_skip();
  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < 100; ++i) {
    CHECK_FALSE(poll(*source).has_value());
  }
  const auto elapsed = std::chrono::steady_clock::now() - start;
  CHECK(elapsed < std::chrono::milliseconds(500));  // a hundred non-blocking polls
  CHECK(source->dropped() == 0);
  CHECK_FALSE(source->quit_requested());
}

TEST_CASE("create: an unknown seat still initialises; it just sees no devices", "[libinput_input_source]") {
  const auto source = create_or_skip(
      LibinputInputSource::Options{.native = kNative, .rotation = Rotation::R0, .seat = "c3-no-such-seat"});
  CHECK(source->descriptor() >= 0);
  CHECK_FALSE(poll(*source).has_value());
}

TEST_CASE("lifetime: the libinput descriptor is closed when the source is destroyed", "[libinput_input_source]") {
  int fd = -1;
  {
    const auto source = create_or_skip();
    fd = source->descriptor();
    REQUIRE(fd >= 0);
    REQUIRE(::fcntl(fd, F_GETFD) != -1);
  }
  CHECK(::fcntl(fd, F_GETFD) == -1);
}

TEST_CASE("lifetime: sources can be created again and two can coexist", "[libinput_input_source]") {
  {
    const auto first = create_or_skip();
    CHECK_FALSE(poll(*first).has_value());
  }
  const auto second = create_or_skip();
  const auto third = create_or_skip();
  CHECK(second->descriptor() >= 0);
  CHECK(third->descriptor() >= 0);
  CHECK(second->descriptor() != third->descriptor());
  CHECK_FALSE(poll(*second).has_value());
  CHECK_FALSE(poll(*third).has_value());
}

// ---------------------------------------------------------------------------
// Tier 2: the real libinput path through a virtual touchscreen (CM4 with udev)
// ---------------------------------------------------------------------------

#define C3_REQUIRE_UINPUT()                                                         \
  if (!VirtualTouchscreen::available()) {                                           \
    SKIP("needs a writable /dev/uinput and a running udev to classify the device"); \
  }

TEST_CASE("rig: a tap at the digitizer centre arrives as Down and Up at the panel centre",
          "[libinput_input_source][rig]") {
  C3_REQUIRE_UINPUT();
  const auto source = create_or_skip();
  VirtualTouchscreen screen;

  const TouchEvent down = first_down(*source, screen, 2048, 2048);
  const Point2 expected = expected_logical(2048, 2048, Rotation::R0);
  CHECK_THAT(down.x, WithinAbs(expected.x, 1.5));
  CHECK_THAT(down.y, WithinAbs(expected.y, 1.5));
  CHECK_THAT(down.x, WithinAbs(kNative.width / 2.0, 1.5));
  CHECK_THAT(down.y, WithinAbs(kNative.height / 2.0, 1.5));

  screen.up(0);
  const auto up = poll_within(*source, std::chrono::milliseconds(500));
  REQUIRE(up.has_value());
  CHECK(up->phase == TouchPhase::Up);
  CHECK(up->id == down.id);
  CHECK(up->x == down.x);  // libinput releases carry no position; the last one is reused
  CHECK(up->y == down.y);
  CHECK(source->dropped() == 0);
}

TEST_CASE("rig: a drag reports moves in order and the release at the last position", "[libinput_input_source][rig]") {
  C3_REQUIRE_UINPUT();
  const auto source = create_or_skip();
  VirtualTouchscreen screen;

  static_cast<void>(first_down(*source, screen, 1024, 1024));
  screen.move(0, 2048, 1024);
  screen.move(0, 2048, 2048);
  screen.up(0);

  std::vector<TouchEvent> events;
  while (const auto e = poll_within(*source, std::chrono::milliseconds(300))) {
    events.push_back(*e);
    if (e->phase == TouchPhase::Up) {
      break;
    }
  }
  REQUIRE(events.size() == 3);
  CHECK(events[0].phase == TouchPhase::Move);
  CHECK(events[1].phase == TouchPhase::Move);
  CHECK(events[2].phase == TouchPhase::Up);
  const Point2 mid = expected_logical(2048, 1024, Rotation::R0);
  const Point2 end = expected_logical(2048, 2048, Rotation::R0);
  CHECK_THAT(events[0].x, WithinAbs(mid.x, 1.5));
  CHECK_THAT(events[0].y, WithinAbs(mid.y, 1.5));
  CHECK_THAT(events[1].x, WithinAbs(end.x, 1.5));
  CHECK_THAT(events[1].y, WithinAbs(end.y, 1.5));
  CHECK(events[2].x == events[1].x);
  CHECK(events[2].y == events[1].y);
}

TEST_CASE("rig: the mounting rotation is applied to touch exactly as it is to drawing",
          "[libinput_input_source][rig]") {
  C3_REQUIRE_UINPUT();
  for (const Rotation r : {Rotation::R90, Rotation::R180, Rotation::R270}) {
    INFO("rotation " << c3::platform::to_degrees(r));
    const auto source = create_or_skip(LibinputInputSource::Options{.native = kNative, .rotation = r});
    VirtualTouchscreen screen;
    const TouchEvent down = first_down(*source, screen, 1024, 2048);  // a quarter in, half down, on the glass
    const Point2 expected = expected_logical(1024, 2048, r);
    CHECK_THAT(down.x, WithinAbs(expected.x, 1.5));
    CHECK_THAT(down.y, WithinAbs(expected.y, 1.5));
    screen.up(0);
    static_cast<void>(poll_within(*source, std::chrono::milliseconds(300)));
  }
}

TEST_CASE("rig: a second finger is ignored while the first is down", "[libinput_input_source][rig]") {
  C3_REQUIRE_UINPUT();
  const auto source = create_or_skip();
  VirtualTouchscreen screen;

  const TouchEvent first = first_down(*source, screen, 1024, 1024);
  screen.down(1, 99, 3072, 3072);
  screen.move(1, 3000, 3000);
  screen.up(1);
  screen.move(0, 1100, 1100);
  screen.up(0);

  std::vector<TouchEvent> events;
  while (const auto e = poll_within(*source, std::chrono::milliseconds(300))) {
    events.push_back(*e);
    if (e->phase == TouchPhase::Up) {
      break;
    }
  }
  REQUIRE(events.size() == 2);
  CHECK(events[0].phase == TouchPhase::Move);
  CHECK(events[0].id == first.id);
  CHECK(events[1].phase == TouchPhase::Up);
  CHECK(events[1].id == first.id);
  const Point2 moved = expected_logical(1100, 1100, Rotation::R0);
  CHECK_THAT(events[0].x, WithinAbs(moved.x, 1.5));
  CHECK_THAT(events[0].y, WithinAbs(moved.y, 1.5));
}

}  // namespace
