#include "logging/log.hpp"
#include "platform/desktop_display_backend.hpp"
#include "platform/input_source.hpp"
#include "platform/panel_viewport.hpp"
#include "platform/sdl_input_source.hpp"

#include <SDL2/SDL.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <vector>

using c3::platform::DesktopBackend;
using c3::platform::InputSource;
using c3::platform::PanelPoint;
using c3::platform::PanelViewport;
using c3::platform::PresentResult;
using c3::platform::SdlInputSource;
using c3::platform::Size;
using c3::platform::TouchEvent;
using c3::platform::TouchPhase;
using c3::platform::window_to_panel;
using c3::platform::window_to_panel_clamped;
using Catch::Matchers::WithinAbs;

CATCH_REGISTER_ENUM(TouchPhase, TouchPhase::Down, TouchPhase::Move, TouchPhase::Up, TouchPhase::Cancel)

namespace {

// ---------------------------------------------------------------------------
// The input source reads SDL's event queue and maps through a real DesktopBackend's viewport,
// so these tests open a window (skipping when there is no display) and inject events with
// SDL_PushEvent. Nothing here depends on a human touching anything. Coordinates pushed as
// mouse events are window points, exactly as SDL reports them; the expected panel point is
// computed from the backend's own viewport so the test holds on any display density.
// ---------------------------------------------------------------------------

constexpr Size kPanel{.width = 400, .height = 640};

void discard_log(void* /*context*/, std::string_view /*line*/) noexcept {}

[[nodiscard]] c3::logging::Sink quiet_sink() noexcept {
  return c3::logging::Sink{.context = nullptr, .emit = &discard_log};
}

/// A backend, its log silencer, and an input source reading from it.
struct Rig {
  std::unique_ptr<c3::logging::ScopedSink> quiet;
  std::unique_ptr<DesktopBackend> backend;
  std::unique_ptr<SdlInputSource> input;

  [[nodiscard]] PanelViewport viewport() const noexcept {
    return backend->viewport();
  }  // by value: the backend hands out a copy

  /// Window point for a panel pixel, inverting the viewport.
  [[nodiscard]] SDL_Point window_point(float panel_x, float panel_y) const noexcept {
    const PanelViewport vp = viewport();
    const float r = vp.pixel_ratio();
    return {static_cast<int>((panel_x * vp.scale + vp.offset_x) / r),
            static_cast<int>((panel_y * vp.scale + vp.offset_y) / r)};
  }

  /// Panel point the source must report for a window point, via the same mapping it uses.
  [[nodiscard]] PanelPoint expected(int wx, int wy) const {
    const auto p = window_to_panel(viewport(), static_cast<float>(wx), static_cast<float>(wy));
    REQUIRE(p.has_value());
    return *p;
  }

  [[nodiscard]] PanelPoint expected_clamped(int wx, int wy) const noexcept {
    return window_to_panel_clamped(viewport(), static_cast<float>(wx), static_cast<float>(wy));
  }

  /// One poll as the render tick would make it, tolerating SDL's poll sentinel.
  ///
  /// SDL marks the end of each pump with a sentinel and SDL_PollEvent reports "no event" when
  /// it reaches it, so that a drain loop cannot spin forever. When the source returns early on a
  /// touch event the sentinel stays queued, and the next poll comes back empty even if an event
  /// was pushed after it. The event loop simply polls again next tick; this helper does the same.
  [[nodiscard]] std::optional<TouchEvent> poll() const noexcept {
    TouchEvent out;
    if (input->poll(out) || input->poll(out)) {
      return out;
    }
    return std::nullopt;
  }

  /// Polls until two consecutive empty polls, so a sentinel in the middle does not stop the drain.
  [[nodiscard]] std::vector<TouchEvent> drain() const noexcept {
    std::vector<TouchEvent> events;
    while (const auto e = poll()) {
      events.push_back(*e);
    }
    return events;
  }

  /// Runs frames until the window really has the requested size (resizes are asynchronous).
  void resize(int window_w, int window_h) const {
    SDL_SetWindowSize(backend->window(), window_w, window_h);
    for (int i = 0; i < 5; ++i) {
      SDL_PumpEvents();
      REQUIRE(backend->begin_frame() != nullptr);
      REQUIRE(backend->end_frame() == PresentResult::Presented);
      int ww = 0;
      int wh = 0;
      SDL_GetWindowSize(backend->window(), &ww, &wh);
      if (ww == window_w && wh == window_h) {
        break;
      }
    }
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
  }
};

[[nodiscard]] Rig rig_or_skip() {
  Rig rig{.quiet = std::make_unique<c3::logging::ScopedSink>(quiet_sink()), .backend = nullptr, .input = nullptr};
  auto result = DesktopBackend::create(
      DesktopBackend::Options{.panel = kPanel, .dpi = 264.0F, .fit_fraction = 0.25F, .title = "c3console input test"});
  if (!result.backend) {
    if (result.error.rfind("SDL_", 0) == 0) {
      SKIP("no display available: " << result.error);
    }
    FAIL("DesktopBackend::create failed: " << result.error);
  }
  rig.backend = std::move(result.backend);
  rig.input = std::make_unique<SdlInputSource>(*rig.backend);
  // Window creation leaves shown/exposed/focus events behind; start from an empty queue.
  SDL_PumpEvents();
  SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
  return rig;
}

// Event injection. SDL_PushEvent queues exactly what we build; no synthesis happens.

void push_mouse_button(Uint32 type, Uint8 button, int x, int y, Uint32 which = 0) {
  SDL_Event e{};
  e.type = type;
  e.button.button = button;
  e.button.which = which;
  e.button.x = x;
  e.button.y = y;
  e.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
  REQUIRE(SDL_PushEvent(&e) == 1);
}

void push_mouse_down(int x, int y, Uint8 button = SDL_BUTTON_LEFT, Uint32 which = 0) {
  push_mouse_button(SDL_MOUSEBUTTONDOWN, button, x, y, which);
}

void push_mouse_up(int x, int y, Uint8 button = SDL_BUTTON_LEFT, Uint32 which = 0) {
  push_mouse_button(SDL_MOUSEBUTTONUP, button, x, y, which);
}

void push_mouse_motion(int x, int y, Uint32 which = 0) {
  SDL_Event e{};
  e.type = SDL_MOUSEMOTION;
  e.motion.which = which;
  e.motion.x = x;
  e.motion.y = y;
  REQUIRE(SDL_PushEvent(&e) == 1);
}

void push_finger(Uint32 type, SDL_FingerID finger, float nx, float ny) {
  SDL_Event e{};
  e.type = type;
  e.tfinger.touchId = 1;
  e.tfinger.fingerId = finger;
  e.tfinger.x = nx;
  e.tfinger.y = ny;
  e.tfinger.pressure = 1.0F;
  REQUIRE(SDL_PushEvent(&e) == 1);
}

void push_window_event(Uint8 event) {
  SDL_Event e{};
  e.type = SDL_WINDOWEVENT;
  e.window.event = event;
  REQUIRE(SDL_PushEvent(&e) == 1);
}

void push_key_down(SDL_Keycode key) {
  SDL_Event e{};
  e.type = SDL_KEYDOWN;
  e.key.state = SDL_PRESSED;
  e.key.keysym.sym = key;
  REQUIRE(SDL_PushEvent(&e) == 1);
}

void push_quit() {
  SDL_Event e{};
  e.type = SDL_QUIT;
  REQUIRE(SDL_PushEvent(&e) == 1);
}

void push_junk(int count) {
  for (int i = 0; i < count; ++i) {
    SDL_Event e{};
    e.type = SDL_USEREVENT;
    REQUIRE(SDL_PushEvent(&e) == 1);
  }
}

void expect_event(const std::optional<TouchEvent>& e, TouchPhase phase, std::int32_t id, PanelPoint at) {
  REQUIRE(e.has_value());
  CHECK(e->phase == phase);
  CHECK(e->id == id);
  CHECK_THAT(e->x, WithinAbs(at.x, 0.01));
  CHECK_THAT(e->y, WithinAbs(at.y, 0.01));
}

// ---------------------------------------------------------------------------
// Type and idle state
// ---------------------------------------------------------------------------

TEST_CASE("SdlInputSource: is an InputSource with a noexcept poll", "[sdl_input_source]") {
  STATIC_CHECK(std::is_base_of_v<InputSource, SdlInputSource>);
  STATIC_CHECK_FALSE(std::is_copy_constructible_v<SdlInputSource>);
  STATIC_CHECK(noexcept(std::declval<SdlInputSource&>().poll(std::declval<TouchEvent&>())));
  STATIC_CHECK(noexcept(std::declval<const SdlInputSource&>().quit_requested()));
}

TEST_CASE("poll: an empty queue yields nothing and no quit", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  CHECK_FALSE(rig.poll().has_value());
  CHECK_FALSE(rig.input->quit_requested());
}

// ---------------------------------------------------------------------------
// Mouse as a single finger with id 0
// ---------------------------------------------------------------------------

TEST_CASE("mouse: a left press inside the panel is a Down with id 0 in panel pixels", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  const SDL_Point w = rig.window_point(100.0F, 300.0F);
  push_mouse_down(w.x, w.y);
  expect_event(rig.poll(), TouchPhase::Down, 0, rig.expected(w.x, w.y));
  CHECK_FALSE(rig.poll().has_value());
}

TEST_CASE("mouse: a press, drag and release arrive in order with their own positions", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  const SDL_Point a = rig.window_point(50.0F, 60.0F);
  const SDL_Point b = rig.window_point(200.0F, 320.0F);
  const SDL_Point c = rig.window_point(350.0F, 600.0F);
  push_mouse_down(a.x, a.y);
  push_mouse_motion(b.x, b.y);
  push_mouse_up(c.x, c.y);

  expect_event(rig.poll(), TouchPhase::Down, 0, rig.expected(a.x, a.y));
  expect_event(rig.poll(), TouchPhase::Move, 0, rig.expected(b.x, b.y));
  expect_event(rig.poll(), TouchPhase::Up, 0, rig.expected(c.x, c.y));  // release position, not the last move
  CHECK_FALSE(rig.poll().has_value());
}

TEST_CASE("mouse: one event per poll; the rest of the queue waits", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  const SDL_Point a = rig.window_point(100.0F, 100.0F);
  push_mouse_down(a.x, a.y);
  push_mouse_up(a.x, a.y);
  const auto first = rig.poll();
  REQUIRE(first.has_value());
  CHECK(first->phase == TouchPhase::Down);
  const auto second = rig.poll();
  REQUIRE(second.has_value());
  CHECK(second->phase == TouchPhase::Up);
  CHECK_FALSE(rig.poll().has_value());
}

TEST_CASE("mouse: motion without a press is not reported", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  const SDL_Point a = rig.window_point(100.0F, 100.0F);
  push_mouse_motion(a.x, a.y);
  push_mouse_motion(a.x + 5, a.y + 5);
  CHECK_FALSE(rig.poll().has_value());
}

TEST_CASE("mouse: a release without a press is not reported", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  const SDL_Point a = rig.window_point(100.0F, 100.0F);
  push_mouse_up(a.x, a.y);
  CHECK_FALSE(rig.poll().has_value());
}

TEST_CASE("mouse: a press outside the window is not ours", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  push_mouse_down(-50, -50);
  push_mouse_down(rig.viewport().window_w + 50, rig.viewport().window_h + 50);
  CHECK(rig.drain().empty());
  // And nothing is latched: a later release is still not reported.
  push_mouse_up(10, 10);
  CHECK_FALSE(rig.poll().has_value());
}

TEST_CASE("mouse: a press in the letterbox is not ours", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  rig.resize(rig.viewport().window_w * 2, rig.viewport().window_h);  // pillarbox both sides
  if (rig.viewport().offset_x < 4.0F) {
    SKIP("window manager refused the resize; no letterbox to press in");
  }
  push_mouse_down(1, rig.viewport().window_h / 2);
  CHECK_FALSE(rig.poll().has_value());

  // A press just inside the panel edge works.
  // A few panel pixels in: one window point spans nearly two panel pixels on a HiDPI display.
  const SDL_Point inside = rig.window_point(8.0F, 320.0F);
  push_mouse_down(inside.x, inside.y);
  expect_event(rig.poll(), TouchPhase::Down, 0, rig.expected(inside.x, inside.y));
}

TEST_CASE("mouse: a drag that leaves the panel is clamped to the edge, never dropped", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  const SDL_Point a = rig.window_point(200.0F, 320.0F);
  push_mouse_down(a.x, a.y);
  push_mouse_motion(-500, 320);   // far left of the window
  push_mouse_motion(5000, 5000);  // far bottom-right
  push_mouse_up(a.x, -500);       // released above the window

  expect_event(rig.poll(), TouchPhase::Down, 0, rig.expected(a.x, a.y));
  expect_event(rig.poll(), TouchPhase::Move, 0, rig.expected_clamped(-500, 320));
  expect_event(rig.poll(), TouchPhase::Move, 0, rig.expected_clamped(5000, 5000));
  expect_event(rig.poll(), TouchPhase::Up, 0, rig.expected_clamped(a.x, -500));
  CHECK_FALSE(rig.poll().has_value());

  const PanelPoint far = rig.expected_clamped(5000, 5000);
  CHECK(far.x == static_cast<float>(kPanel.width) - 1.0F);
  CHECK(far.y == static_cast<float>(kPanel.height) - 1.0F);
}

TEST_CASE("mouse: the Up always matches the Down even when released off the panel", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  const SDL_Point a = rig.window_point(200.0F, 320.0F);
  push_mouse_down(a.x, a.y);
  push_mouse_up(-100, -100);
  expect_event(rig.poll(), TouchPhase::Down, 0, rig.expected(a.x, a.y));
  expect_event(rig.poll(), TouchPhase::Up, 0, PanelPoint{.x = 0.0F, .y = 0.0F});
}

TEST_CASE("mouse: a second press while pressed is ignored", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  const SDL_Point a = rig.window_point(100.0F, 100.0F);
  const SDL_Point b = rig.window_point(300.0F, 500.0F);
  push_mouse_down(a.x, a.y);
  push_mouse_down(b.x, b.y);
  push_mouse_up(b.x, b.y);
  expect_event(rig.poll(), TouchPhase::Down, 0, rig.expected(a.x, a.y));
  expect_event(rig.poll(), TouchPhase::Up, 0, rig.expected(b.x, b.y));
  CHECK_FALSE(rig.poll().has_value());
}

TEST_CASE("mouse: after a release a new press starts a new gesture", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  const SDL_Point a = rig.window_point(100.0F, 100.0F);
  const SDL_Point b = rig.window_point(300.0F, 500.0F);
  push_mouse_down(a.x, a.y);
  push_mouse_up(a.x, a.y);
  push_mouse_down(b.x, b.y);
  const auto events = rig.drain();
  REQUIRE(events.size() == 3);
  CHECK(events[0].phase == TouchPhase::Down);
  CHECK(events[1].phase == TouchPhase::Up);
  CHECK(events[2].phase == TouchPhase::Down);
  CHECK_THAT(events[2].x, WithinAbs(rig.expected(b.x, b.y).x, 0.01));
}

TEST_CASE("mouse: buttons other than the left are ignored entirely", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  const SDL_Point a = rig.window_point(100.0F, 100.0F);
  push_mouse_down(a.x, a.y, SDL_BUTTON_RIGHT);
  push_mouse_up(a.x, a.y, SDL_BUTTON_RIGHT);
  push_mouse_down(a.x, a.y, SDL_BUTTON_MIDDLE);
  CHECK(rig.drain().empty());

  // A right release does not end a left gesture.
  push_mouse_down(a.x, a.y);
  push_mouse_up(a.x, a.y, SDL_BUTTON_RIGHT);
  push_mouse_motion(a.x + 2, a.y + 2);
  const auto events = rig.drain();
  REQUIRE(events.size() == 2);
  CHECK(events[0].phase == TouchPhase::Down);
  CHECK(events[1].phase == TouchPhase::Move);
}

TEST_CASE("mouse: the mouse echo SDL synthesises for touch is ignored", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  const SDL_Point a = rig.window_point(100.0F, 100.0F);
  push_mouse_down(a.x, a.y, SDL_BUTTON_LEFT, SDL_TOUCH_MOUSEID);
  push_mouse_motion(a.x + 1, a.y + 1, SDL_TOUCH_MOUSEID);
  push_mouse_up(a.x, a.y, SDL_BUTTON_LEFT, SDL_TOUCH_MOUSEID);
  CHECK(rig.drain().empty());
}

// ---------------------------------------------------------------------------
// Fingers
// ---------------------------------------------------------------------------

TEST_CASE("finger: normalised coordinates are scaled to the window, then mapped to the panel", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  const PanelViewport vp = rig.viewport();
  push_finger(SDL_FINGERDOWN, 7, 0.5F, 0.5F);
  const auto centre =
      window_to_panel(vp, 0.5F * static_cast<float>(vp.window_w), 0.5F * static_cast<float>(vp.window_h));
  REQUIRE(centre.has_value());
  expect_event(rig.poll(), TouchPhase::Down, 7, *centre);
  CHECK_THAT(centre->x, WithinAbs(kPanel.width / 2.0, 1.5));  // the window centre is the panel centre
  CHECK_THAT(centre->y, WithinAbs(kPanel.height / 2.0, 1.5));
}

TEST_CASE("finger: down, motion and up carry the finger id", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  push_finger(SDL_FINGERDOWN, 3, 0.25F, 0.25F);
  push_finger(SDL_FINGERMOTION, 3, 0.5F, 0.5F);
  push_finger(SDL_FINGERUP, 3, 0.75F, 0.75F);
  const auto events = rig.drain();
  REQUIRE(events.size() == 3);
  CHECK(events[0].phase == TouchPhase::Down);
  CHECK(events[1].phase == TouchPhase::Move);
  CHECK(events[2].phase == TouchPhase::Up);
  for (const auto& e : events) {
    CHECK(e.id == 3);
  }
  CHECK(events[0].x < events[1].x);
  CHECK(events[1].x < events[2].x);
}

TEST_CASE("finger: 64-bit ids keep their low 31 bits", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  push_finger(SDL_FINGERDOWN, static_cast<SDL_FingerID>(0x1'0000'0005LL), 0.5F, 0.5F);
  auto e = rig.poll();
  REQUIRE(e.has_value());
  CHECK(e->id == 5);
  push_finger(SDL_FINGERUP, static_cast<SDL_FingerID>(0x1'0000'0005LL), 0.5F, 0.5F);
  e = rig.poll();
  REQUIRE(e.has_value());
  CHECK(e->phase == TouchPhase::Up);
  CHECK(e->id == 5);

  push_finger(SDL_FINGERDOWN, static_cast<SDL_FingerID>(-1), 0.5F, 0.5F);
  e = rig.poll();
  REQUIRE(e.has_value());
  CHECK(e->id == 0x7fffffff);
}

TEST_CASE("finger: a second finger during a gesture is ignored and the first continues", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  push_finger(SDL_FINGERDOWN, 1, 0.5F, 0.5F);
  push_finger(SDL_FINGERDOWN, 2, 0.6F, 0.6F);
  push_finger(SDL_FINGERMOTION, 2, 0.7F, 0.7F);
  push_finger(SDL_FINGERMOTION, 1, 0.4F, 0.4F);
  push_finger(SDL_FINGERUP, 2, 0.7F, 0.7F);
  push_finger(SDL_FINGERUP, 1, 0.4F, 0.4F);
  const auto events = rig.drain();
  REQUIRE(events.size() == 3);
  for (const auto& e : events) {
    CHECK(e.id == 1);
  }
  CHECK(events[0].phase == TouchPhase::Down);
  CHECK(events[1].phase == TouchPhase::Move);
  CHECK(events[2].phase == TouchPhase::Up);
}

TEST_CASE("finger: a finger that lands outside the window is not ours", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  push_finger(SDL_FINGERDOWN, 9, 1.5F, 0.5F);
  push_finger(SDL_FINGERDOWN, 9, -0.2F, 0.5F);
  push_finger(SDL_FINGERUP, 9, 0.5F, 0.5F);
  CHECK(rig.drain().empty());
}

TEST_CASE("finger: a drag off the window is clamped like the mouse", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  push_finger(SDL_FINGERDOWN, 4, 0.5F, 0.5F);
  push_finger(SDL_FINGERMOTION, 4, 2.0F, 2.0F);
  push_finger(SDL_FINGERUP, 4, -1.0F, -1.0F);
  const auto events = rig.drain();
  REQUIRE(events.size() == 3);
  CHECK(events[1].x == static_cast<float>(kPanel.width) - 1.0F);
  CHECK(events[1].y == static_cast<float>(kPanel.height) - 1.0F);
  CHECK(events[2].phase == TouchPhase::Up);
  CHECK(events[2].x == 0.0F);
  CHECK(events[2].y == 0.0F);
}

TEST_CASE("finger and mouse: whichever presses first owns the gesture", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  const SDL_Point a = rig.window_point(100.0F, 100.0F);
  push_finger(SDL_FINGERDOWN, 6, 0.5F, 0.5F);
  push_mouse_down(a.x, a.y);  // ignored: a finger is already down
  push_mouse_up(a.x, a.y);    // ignored: wrong id
  push_finger(SDL_FINGERUP, 6, 0.5F, 0.5F);
  const auto events = rig.drain();
  REQUIRE(events.size() == 2);
  CHECK(events[0].id == 6);
  CHECK(events[1].id == 6);
  CHECK(events[1].phase == TouchPhase::Up);
}

// ---------------------------------------------------------------------------
// Quit and focus
// ---------------------------------------------------------------------------

TEST_CASE("quit: SDL_QUIT sets the flag without producing a touch event", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  push_quit();
  CHECK_FALSE(rig.poll().has_value());
  CHECK(rig.input->quit_requested());
  CHECK_FALSE(rig.poll().has_value());
  CHECK(rig.input->quit_requested());  // sticky
}

TEST_CASE("quit: Escape requests quit, other keys do not", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  push_key_down(SDLK_a);
  push_key_down(SDLK_RETURN);
  push_key_down(SDLK_SPACE);
  CHECK_FALSE(rig.poll().has_value());
  CHECK_FALSE(rig.input->quit_requested());
  push_key_down(SDLK_ESCAPE);
  CHECK_FALSE(rig.poll().has_value());
  CHECK(rig.input->quit_requested());
}

TEST_CASE("quit: a quit in the same batch as a press does not swallow the press", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  const SDL_Point a = rig.window_point(100.0F, 100.0F);
  push_quit();
  push_mouse_down(a.x, a.y);
  expect_event(rig.poll(), TouchPhase::Down, 0, rig.expected(a.x, a.y));
  CHECK(rig.input->quit_requested());
}

TEST_CASE("focus: losing focus cancels an active gesture at its last position", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  const SDL_Point a = rig.window_point(100.0F, 100.0F);
  const SDL_Point b = rig.window_point(150.0F, 200.0F);
  push_mouse_down(a.x, a.y);
  push_mouse_motion(b.x, b.y);
  push_window_event(SDL_WINDOWEVENT_FOCUS_LOST);
  expect_event(rig.poll(), TouchPhase::Down, 0, rig.expected(a.x, a.y));
  expect_event(rig.poll(), TouchPhase::Move, 0, rig.expected(b.x, b.y));
  expect_event(rig.poll(), TouchPhase::Cancel, 0, rig.expected(b.x, b.y));
  CHECK_FALSE(rig.poll().has_value());

  // The release that follows belongs to a gesture that no longer exists.
  push_mouse_up(b.x, b.y);
  CHECK_FALSE(rig.poll().has_value());
}

TEST_CASE("focus: losing focus with nothing pressed is silent", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  push_window_event(SDL_WINDOWEVENT_FOCUS_LOST);
  CHECK_FALSE(rig.poll().has_value());
  CHECK_FALSE(rig.input->quit_requested());
}

TEST_CASE("focus: other window events are ignored", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  const SDL_Point a = rig.window_point(100.0F, 100.0F);
  push_mouse_down(a.x, a.y);
  push_window_event(SDL_WINDOWEVENT_FOCUS_GAINED);
  push_window_event(SDL_WINDOWEVENT_MOVED);
  push_window_event(SDL_WINDOWEVENT_EXPOSED);
  push_window_event(SDL_WINDOWEVENT_SIZE_CHANGED);
  push_mouse_up(a.x, a.y);
  const auto events = rig.drain();
  REQUIRE(events.size() == 2);
  CHECK(events[0].phase == TouchPhase::Down);
  CHECK(events[1].phase == TouchPhase::Up);
}

// ---------------------------------------------------------------------------
// Event budget
// ---------------------------------------------------------------------------

TEST_CASE("poll: drains at most 64 events per call, so a flood cannot stall the render tick", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  const SDL_Point a = rig.window_point(100.0F, 100.0F);
  push_junk(70);
  push_mouse_down(a.x, a.y);
  TouchEvent out;
  CHECK_FALSE(rig.input->poll(out));  // raw poll: 64 junk events consumed, the press not yet reached
  REQUIRE(rig.input->poll(out));      // 6 junk, then the press
  CHECK(out.phase == TouchPhase::Down);
  CHECK_THAT(out.x, WithinAbs(rig.expected(a.x, a.y).x, 0.01));
}

TEST_CASE("poll: SDL's poll sentinel ends a drain early; the event loop picks the rest up next tick",
          "[sdl_input_source]") {
  // Documented so the behaviour is not mistaken for a lost event: after the source returns a
  // touch event, SDL's end-of-pump sentinel is still queued. A raw poll then reports nothing
  // even though a newly arrived event sits behind the sentinel; the following poll delivers it.
  const Rig rig = rig_or_skip();
  const SDL_Point a = rig.window_point(100.0F, 100.0F);
  TouchEvent out;
  push_mouse_down(a.x, a.y);
  REQUIRE(rig.input->poll(out));
  CHECK(out.phase == TouchPhase::Down);

  push_mouse_up(a.x, a.y);
  CHECK_FALSE(rig.input->poll(out));  // the sentinel from the previous pump
  REQUIRE(rig.input->poll(out));      // next tick: the release
  CHECK(out.phase == TouchPhase::Up);
  CHECK_FALSE(rig.input->poll(out));
}

TEST_CASE("poll: irrelevant events between touch events do not break the sequence", "[sdl_input_source]") {
  const Rig rig = rig_or_skip();
  const SDL_Point a = rig.window_point(100.0F, 100.0F);
  push_junk(10);
  push_mouse_down(a.x, a.y);
  push_junk(10);
  push_key_down(SDLK_b);
  push_mouse_motion(a.x + 3, a.y + 3);
  push_junk(10);
  push_mouse_up(a.x + 3, a.y + 3);
  push_junk(10);
  const auto events = rig.drain();
  REQUIRE(events.size() == 3);
  CHECK(events[0].phase == TouchPhase::Down);
  CHECK(events[1].phase == TouchPhase::Move);
  CHECK(events[2].phase == TouchPhase::Up);
  CHECK_FALSE(rig.input->quit_requested());
}

}  // namespace
