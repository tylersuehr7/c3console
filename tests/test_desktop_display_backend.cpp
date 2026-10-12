#include "logging/log.hpp"
#include "nanovg.h"
#include "platform/debug_frame_capture.hpp"
#include "platform/desktop_display_backend.hpp"
#include "platform/display_backend.hpp"
#include "platform/gl_headers.hpp"
#include "platform/panel_viewport.hpp"

#include <SDL2/SDL.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

using c3::platform::DebugFrameCapture;
using c3::platform::DesktopBackend;
using c3::platform::DisplayBackend;
using c3::platform::fit_panel;
using c3::platform::PanelViewport;
using c3::platform::PresentResult;
using c3::platform::Size;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

namespace {

// ---------------------------------------------------------------------------
// These tests open a real SDL window and GL context. On a machine with no display SDL
// fails at init or window creation; those cases skip instead of failing. A small panel
// and a small share of the screen keep each window cheap.
// ---------------------------------------------------------------------------

constexpr Size kPanel{.width = 400, .height = 640};

[[nodiscard]] DesktopBackend::Options small_options() {
  return DesktopBackend::Options{.panel = kPanel, .dpi = 264.0F, .fit_fraction = 0.25F, .title = "c3console test"};
}

/// The backend logs through the project logger on create and on capture; keep that out of the
/// test output. The sink is installed per test case and restored on exit, as the logging tests do.
void discard_log(void* /*context*/, std::string_view /*line*/) noexcept {}

[[nodiscard]] c3::logging::Sink quiet_sink() noexcept {
  return c3::logging::Sink{.context = nullptr, .emit = &discard_log};
}

/// A created backend together with the log silencer that must outlive it.
struct Opened {
  std::unique_ptr<c3::logging::ScopedSink> quiet;  // heap-held so Opened stays movable
  std::unique_ptr<DesktopBackend> backend;
  [[nodiscard]] DesktopBackend* operator->() const noexcept { return backend.get(); }
  [[nodiscard]] DesktopBackend& operator*() const noexcept { return *backend; }
};

[[nodiscard]] Opened create_or_skip(const DesktopBackend::Options& options) {
  Opened opened{.quiet = std::make_unique<c3::logging::ScopedSink>(quiet_sink()), .backend = nullptr};
  auto result = DesktopBackend::create(options);
  if (!result.backend) {
    if (result.error.rfind("SDL_", 0) == 0) {
      SKIP("no display available: " << result.error);
    }
    FAIL("DesktopBackend::create failed: " << result.error);
  }
  REQUIRE(result.error.empty());
  opened.backend = std::move(result.backend);
  return opened;
}

[[nodiscard]] Opened create_or_skip() {
  return create_or_skip(small_options());
}

/// A fresh scratch directory, removed when the test case ends.
class ScratchDir {
public:
  ScratchDir() {
    static unsigned counter = 0;
    path_ = std::filesystem::temp_directory_path() /
            ("c3console_desktop_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
    std::filesystem::remove_all(path_);
    REQUIRE(std::filesystem::create_directory(path_));
  }
  ~ScratchDir() { std::filesystem::remove_all(path_); }
  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;
  ScratchDir(ScratchDir&&) = delete;
  ScratchDir& operator=(ScratchDir&&) = delete;
  [[nodiscard]] std::string file(std::string_view name) const { return (path_ / name).string(); }
  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
  std::filesystem::path path_;
};

/// A decoded binary PPM as written by the capture path.
struct Ppm {
  int width = 0;
  int height = 0;
  std::string pixels;  // RGB, top row first

  struct Rgb {
    int r;
    int g;
    int b;
  };

  [[nodiscard]] Rgb at(int x, int y) const {
    REQUIRE(x >= 0);
    REQUIRE(y >= 0);
    REQUIRE(x < width);
    REQUIRE(y < height);
    const auto i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 3;
    return {static_cast<unsigned char>(pixels[i]), static_cast<unsigned char>(pixels[i + 1]),
            static_cast<unsigned char>(pixels[i + 2])};
  }
};

[[nodiscard]] Ppm read_ppm(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.is_open());
  const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  Ppm ppm;
  int max = 0;
  int consumed = 0;
  REQUIRE(std::sscanf(content.c_str(), "P6\n%d %d\n%d\n%n", &ppm.width, &ppm.height, &max, &consumed) == 3);
  CHECK(max == 255);
  ppm.pixels = content.substr(static_cast<std::size_t>(consumed));
  REQUIRE(ppm.pixels.size() == static_cast<std::size_t>(ppm.width) * static_cast<std::size_t>(ppm.height) * 3);
  return ppm;
}

[[nodiscard]] bool near_colour(Ppm::Rgb c, int r, int g, int b, int tol = 3) noexcept {
  return std::abs(c.r - r) <= tol && std::abs(c.g - g) <= tol && std::abs(c.b - b) <= tol;
}

/// Paints the top half of the panel red and the bottom half blue, in panel pixels.
void draw_two_tone(NVGcontext* vg, Size panel) {
  const auto w = static_cast<float>(panel.width);
  const auto h = static_cast<float>(panel.height);
  nvgBeginPath(vg);
  nvgRect(vg, 0.0F, 0.0F, w, h / 2);
  nvgFillColor(vg, nvgRGB(255, 0, 0));
  nvgFill(vg);
  nvgBeginPath(vg);
  nvgRect(vg, 0.0F, h / 2, w, h / 2);
  nvgFillColor(vg, nvgRGB(0, 0, 255));
  nvgFill(vg);
}

/// Draws one frame of the two-tone pattern and presents it.
void present_two_tone(DesktopBackend& backend) {
  NVGcontext* vg = backend.begin_frame();
  REQUIRE(vg != nullptr);
  draw_two_tone(vg, backend.size());
  REQUIRE(backend.end_frame() == PresentResult::Presented);
}

/// Resizes the window and runs a frame so the backend refreshes its viewport.
void resize_and_settle(DesktopBackend& backend, int window_w, int window_h) {
  SDL_SetWindowSize(backend.window(), window_w, window_h);
  for (int i = 0; i < 5; ++i) {
    SDL_PumpEvents();
    REQUIRE(backend.begin_frame() != nullptr);
    REQUIRE(backend.end_frame() == PresentResult::Presented);
    int ww = 0;
    int wh = 0;
    SDL_GetWindowSize(backend.window(), &ww, &wh);
    if (ww == window_w && wh == window_h) {
      return;
    }
  }
}

// ---------------------------------------------------------------------------
// No display required
// ---------------------------------------------------------------------------

TEST_CASE("DesktopBackend: options default to the 2x panel, the target DPI and a 90 percent window",
          "[desktop_display_backend]") {
  constexpr DesktopBackend::Options defaults{};
  STATIC_CHECK(defaults.panel == Size{.width = 1600, .height = 2560});
  STATIC_CHECK(defaults.dpi == 264.0F);
  STATIC_CHECK(defaults.fit_fraction == 0.9F);
  STATIC_CHECK(defaults.title == "C3Console");
}

TEST_CASE("DesktopBackend: is a final, non-copyable DisplayBackend and DebugFrameCapture",
          "[desktop_display_backend]") {
  STATIC_CHECK(std::is_base_of_v<DisplayBackend, DesktopBackend>);
  STATIC_CHECK(std::is_base_of_v<DebugFrameCapture, DesktopBackend>);
  STATIC_CHECK(std::is_final_v<DesktopBackend>);
  STATIC_CHECK_FALSE(std::is_copy_constructible_v<DesktopBackend>);
  STATIC_CHECK_FALSE(std::is_move_constructible_v<DesktopBackend>);
  STATIC_CHECK_FALSE(std::is_default_constructible_v<DesktopBackend>);
  STATIC_CHECK(noexcept(std::declval<DesktopBackend&>().begin_frame()));
  STATIC_CHECK(noexcept(std::declval<DesktopBackend&>().end_frame()));
  STATIC_CHECK(noexcept(std::declval<DesktopBackend&>().request_capture(std::string_view{})));
  STATIC_CHECK(noexcept(std::declval<const DesktopBackend&>().size()));
  STATIC_CHECK(noexcept(std::declval<const DesktopBackend&>().viewport()));
}

TEST_CASE("create: a non-positive panel is refused before SDL is touched", "[desktop_display_backend]") {
  const bool video_before = SDL_WasInit(SDL_INIT_VIDEO) != 0;
  for (const Size bad : {Size{.width = 0, .height = 640}, Size{.width = 400, .height = 0},
                         Size{.width = -1, .height = 640}, Size{.width = 400, .height = -10}}) {
    INFO("panel " << bad.width << "x" << bad.height);
    DesktopBackend::Options options = small_options();
    options.panel = bad;
    const auto result = DesktopBackend::create(options);
    CHECK(result.backend == nullptr);
    CHECK(result.error == "panel size must be positive");
  }
  CHECK((SDL_WasInit(SDL_INIT_VIDEO) != 0) == video_before);
}

// ---------------------------------------------------------------------------
// Creation
// ---------------------------------------------------------------------------

TEST_CASE("create: opens a window and a GL context and reports the configured panel", "[desktop_display_backend]") {
  const auto backend = create_or_skip();
  CHECK(backend->size() == kPanel);
  CHECK(backend->dpi() == 264.0F);
  CHECK(backend->context() != nullptr);
  REQUIRE(backend->window() != nullptr);
  CHECK(SDL_WasInit(SDL_INIT_VIDEO) != 0);
  CHECK(std::string_view(SDL_GetWindowTitle(backend->window())) == "c3console test");

  const Uint32 flags = SDL_GetWindowFlags(backend->window());
  CHECK((flags & SDL_WINDOW_OPENGL) != 0);
  CHECK((flags & SDL_WINDOW_RESIZABLE) != 0);
  CHECK((flags & SDL_WINDOW_ALLOW_HIGHDPI) != 0);
}

TEST_CASE("create: the DPI is the target panel's, whatever the laptop reports", "[desktop_display_backend]") {
  DesktopBackend::Options options = small_options();
  options.dpi = 132.5F;
  const auto backend = create_or_skip(options);
  CHECK(backend->dpi() == 132.5F);
}

TEST_CASE("create: the GL context is at least 3.2 core with a stencil buffer, and is current",
          "[desktop_display_backend]") {
  const auto backend = create_or_skip();
  int major = 0;
  int minor = 0;
  int profile = 0;
  int stencil = 0;
  int doublebuffer = 0;
  REQUIRE(SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major) == 0);
  REQUIRE(SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minor) == 0);
  REQUIRE(SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &profile) == 0);
  REQUIRE(SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &stencil) == 0);
  REQUIRE(SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER, &doublebuffer) == 0);
  CHECK((major > 3 || (major == 3 && minor >= 2)));
  CHECK((profile & SDL_GL_CONTEXT_PROFILE_CORE) != 0);
  CHECK(stencil >= 8);  // NanoVG stencil strokes
  CHECK(doublebuffer == 1);

  CHECK(SDL_GL_GetCurrentWindow() == backend->window());
  CHECK(SDL_GL_GetCurrentContext() != nullptr);
  const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));  // NOLINT
  REQUIRE(version != nullptr);
  CHECK(std::string_view(version).size() > 0);
}

TEST_CASE("create: the window takes the requested share of the display and keeps the panel's aspect",
          "[desktop_display_backend]") {
  const auto backend = create_or_skip();
  SDL_Rect usable{};
  REQUIRE(SDL_GetDisplayUsableBounds(0, &usable) == 0);
  int ww = 0;
  int wh = 0;
  SDL_GetWindowSize(backend->window(), &ww, &wh);
  CHECK(ww > 0);
  CHECK(wh > 0);
  CHECK(static_cast<float>(ww) <= static_cast<float>(usable.w) * 0.25F + 1.0F);
  CHECK(static_cast<float>(wh) <= static_cast<float>(usable.h) * 0.25F + 1.0F);
  const double window_aspect = static_cast<double>(ww) / static_cast<double>(wh);
  const double panel_aspect = static_cast<double>(kPanel.width) / static_cast<double>(kPanel.height);
  CHECK_THAT(window_aspect, WithinAbs(panel_aspect, 0.02));
}

TEST_CASE("create: a larger fit fraction gives a larger window", "[desktop_display_backend]") {
  int small_w = 0;
  int small_h = 0;
  {
    const auto small = create_or_skip();
    SDL_GetWindowSize(small->window(), &small_w, &small_h);
  }
  DesktopBackend::Options options = small_options();
  options.fit_fraction = 0.5F;
  const auto bigger = create_or_skip(options);
  int big_w = 0;
  int big_h = 0;
  SDL_GetWindowSize(bigger->window(), &big_w, &big_h);
  CHECK(big_w > small_w);
  CHECK(big_h > small_h);
  CHECK_THAT(static_cast<double>(big_w) / small_w, WithinAbs(2.0, 0.1));
}

TEST_CASE("create: the initial viewport is fit_panel of the real window and drawable", "[desktop_display_backend]") {
  const auto backend = create_or_skip();
  int dw = 0;
  int dh = 0;
  int ww = 0;
  int wh = 0;
  SDL_GL_GetDrawableSize(backend->window(), &dw, &dh);
  SDL_GetWindowSize(backend->window(), &ww, &wh);
  const PanelViewport expected = fit_panel(kPanel, dw, dh, ww, wh);
  const PanelViewport vp = backend->viewport();
  CHECK(vp.panel == kPanel);
  CHECK(vp.drawable_w == dw);
  CHECK(vp.drawable_h == dh);
  CHECK(vp.window_w == ww);
  CHECK(vp.window_h == wh);
  CHECK(vp.scale == expected.scale);
  CHECK(vp.offset_x == expected.offset_x);
  CHECK(vp.offset_y == expected.offset_y);
  CHECK(vp.scale > 0.0F);
  CHECK(vp.pixel_ratio() >= 1.0F);
  CHECK(std::min(vp.offset_x, vp.offset_y) == 0.0F);
}

// ---------------------------------------------------------------------------
// Frame protocol
// ---------------------------------------------------------------------------

TEST_CASE("frame: begin returns the NanoVG context and end presents", "[desktop_display_backend]") {
  const auto backend = create_or_skip();
  NVGcontext* vg = backend->begin_frame();
  REQUIRE(vg != nullptr);
  CHECK(vg == backend->context());
  CHECK(backend->end_frame() == PresentResult::Presented);
}

TEST_CASE("frame: end without begin fails and begin inside a frame returns null", "[desktop_display_backend]") {
  const auto backend = create_or_skip();
  CHECK(backend->end_frame() == PresentResult::Failed);

  REQUIRE(backend->begin_frame() != nullptr);
  CHECK(backend->begin_frame() == nullptr);                 // nested begin
  CHECK(backend->end_frame() == PresentResult::Presented);  // the open frame is intact
  CHECK(backend->end_frame() == PresentResult::Failed);     // and now closed
}

TEST_CASE("frame: many frames in a row all present", "[desktop_display_backend]") {
  const auto backend = create_or_skip();
  for (int i = 0; i < 10; ++i) {
    INFO("frame " << i);
    REQUIRE(backend->begin_frame() != nullptr);
    REQUIRE(backend->end_frame() == PresentResult::Presented);
  }
}

TEST_CASE("frame: begin_frame refreshes the viewport after the window is resized", "[desktop_display_backend]") {
  const auto backend = create_or_skip();
  const PanelViewport before = backend->viewport();
  int ww = 0;
  int wh = 0;
  SDL_GetWindowSize(backend->window(), &ww, &wh);

  resize_and_settle(*backend, ww * 2, wh);  // twice as wide: the panel is pillarboxed
  const PanelViewport after = backend->viewport();
  int dw = 0;
  int dh = 0;
  SDL_GL_GetDrawableSize(backend->window(), &dw, &dh);
  CHECK(after.drawable_w == dw);
  CHECK(after.drawable_h == dh);
  CHECK(after.drawable_w > before.drawable_w);
  CHECK(after.offset_x > before.offset_x);
  CHECK(after.offset_y == 0.0F);
  CHECK_THAT(after.scale, WithinAbs(before.scale, 0.05));  // height still limits the scale
}

// ---------------------------------------------------------------------------
// Frame capture
// ---------------------------------------------------------------------------

TEST_CASE("request_capture: rejects an empty path and a path longer than 255 bytes", "[desktop_display_backend]") {
  const auto backend = create_or_skip();
  CHECK_FALSE(backend->request_capture(""));
  CHECK_FALSE(backend->request_capture(std::string(256, 'p')));
  CHECK(backend->request_capture(std::string(255, 'p')));  // kept, even if it later fails to open
}

TEST_CASE("request_capture: only one capture may be pending at a time", "[desktop_display_backend]") {
  const ScratchDir dir;
  const auto backend = create_or_skip();
  CHECK(backend->request_capture(dir.file("a.ppm")));
  CHECK_FALSE(backend->request_capture(dir.file("b.ppm")));
  present_two_tone(*backend);  // writes a.ppm and clears the pending flag
  CHECK(std::filesystem::exists(dir.file("a.ppm")));
  CHECK_FALSE(std::filesystem::exists(dir.file("b.ppm")));
  CHECK(backend->request_capture(dir.file("b.ppm")));
}

TEST_CASE("capture: writes a binary PPM of the drawable at the end of the frame", "[desktop_display_backend]") {
  const ScratchDir dir;
  const auto backend = create_or_skip();
  const std::string path = dir.file("frame.ppm");
  REQUIRE(backend->request_capture(path));
  CHECK_FALSE(std::filesystem::exists(path));  // nothing until a frame ends
  present_two_tone(*backend);
  REQUIRE(std::filesystem::exists(path));

  const Ppm ppm = read_ppm(path);
  const PanelViewport vp = backend->viewport();
  CHECK(ppm.width == vp.drawable_w);
  CHECK(ppm.height == vp.drawable_h);
}

TEST_CASE("capture: the image is the right way up and the panel lands where the viewport says",
          "[desktop_display_backend]") {
  const ScratchDir dir;
  const auto backend = create_or_skip();
  const std::string path = dir.file("frame.ppm");
  REQUIRE(backend->request_capture(path));
  present_two_tone(*backend);

  const Ppm ppm = read_ppm(path);
  const PanelViewport vp = backend->viewport();
  const auto px = [&](float panel_x, float panel_y) {
    return ppm.at(static_cast<int>(vp.offset_x + panel_x * vp.scale),
                  static_cast<int>(vp.offset_y + panel_y * vp.scale));
  };
  const auto pw = static_cast<float>(kPanel.width);
  const auto ph = static_cast<float>(kPanel.height);

  const Ppm::Rgb top = px(pw / 2, ph / 4);
  const Ppm::Rgb bottom = px(pw / 2, ph * 3 / 4);
  INFO("top (" << top.r << "," << top.g << "," << top.b << ") bottom (" << bottom.r << "," << bottom.g << ","
               << bottom.b << ")");
  CHECK(near_colour(top, 255, 0, 0));  // red half is at the top of the file, not flipped
  CHECK(near_colour(bottom, 0, 0, 255));
  CHECK(near_colour(px(2.0F, 2.0F), 255, 0, 0));            // top-left corner of the panel
  CHECK(near_colour(px(pw - 3.0F, ph - 3.0F), 0, 0, 255));  // bottom-right corner
}

TEST_CASE("capture: the letterbox outside the panel is the cleared black", "[desktop_display_backend]") {
  const ScratchDir dir;
  const auto backend = create_or_skip();
  int ww = 0;
  int wh = 0;
  SDL_GetWindowSize(backend->window(), &ww, &wh);
  resize_and_settle(*backend, ww * 2, wh);  // force a pillarbox on both sides
  const PanelViewport vp = backend->viewport();
  if (vp.offset_x < 4.0F) {
    SKIP("window manager refused the resize; no letterbox to inspect");
  }

  const std::string path = dir.file("frame.ppm");
  REQUIRE(backend->request_capture(path));
  present_two_tone(*backend);
  const Ppm ppm = read_ppm(path);

  const Ppm::Rgb left = ppm.at(1, ppm.height / 2);
  const Ppm::Rgb right = ppm.at(ppm.width - 2, ppm.height / 2);
  const Ppm::Rgb inside = ppm.at(static_cast<int>(vp.offset_x + 5.0F), static_cast<int>(vp.offset_y + 5.0F));
  CHECK(near_colour(left, 0, 0, 0));
  CHECK(near_colour(right, 0, 0, 0));
  CHECK(near_colour(inside, 255, 0, 0));
}

TEST_CASE("capture: requested mid-frame it is written at that frame's end", "[desktop_display_backend]") {
  const ScratchDir dir;
  const auto backend = create_or_skip();
  const std::string path = dir.file("mid.ppm");
  NVGcontext* vg = backend->begin_frame();
  REQUIRE(vg != nullptr);
  draw_two_tone(vg, kPanel);
  REQUIRE(backend->request_capture(path));
  CHECK_FALSE(std::filesystem::exists(path));
  REQUIRE(backend->end_frame() == PresentResult::Presented);
  CHECK(std::filesystem::exists(path));
  const Ppm ppm = read_ppm(path);
  const PanelViewport vp = backend->viewport();
  CHECK(near_colour(ppm.at(static_cast<int>(vp.offset_x + 5.0F), static_cast<int>(vp.offset_y + 5.0F)), 255, 0, 0));
}

TEST_CASE("capture: an unwritable path is reported, the frame still presents, and the flag clears",
          "[desktop_display_backend]") {
  const ScratchDir dir;
  const auto backend = create_or_skip();
  const std::string path = dir.file("missing/dir/frame.ppm");
  REQUIRE(backend->request_capture(path));
  REQUIRE(backend->begin_frame() != nullptr);
  CHECK(backend->end_frame() == PresentResult::Presented);
  CHECK_FALSE(std::filesystem::exists(path));
  CHECK(backend->request_capture(dir.file("next.ppm")));  // not stuck pending
}

TEST_CASE("capture: a 255-byte path is written", "[desktop_display_backend]") {
  const ScratchDir dir;
  const auto backend = create_or_skip();
  const std::string prefix = dir.file("");
  REQUIRE(prefix.size() < 200);
  const std::string path = prefix + std::string(255 - prefix.size() - 4, 'n') + ".ppm";
  REQUIRE(path.size() == 255);
  REQUIRE(backend->request_capture(path));
  present_two_tone(*backend);
  CHECK(std::filesystem::exists(path));
}

TEST_CASE("capture: each frame can be captured in turn", "[desktop_display_backend]") {
  const ScratchDir dir;
  const auto backend = create_or_skip();
  for (int i = 0; i < 3; ++i) {
    const std::string path = dir.file("frame" + std::to_string(i) + ".ppm");
    REQUIRE(backend->request_capture(path));
    present_two_tone(*backend);
    REQUIRE(std::filesystem::exists(path));
  }
}

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

TEST_CASE("lifetime: destroying the backend releases the SDL video subsystem it started", "[desktop_display_backend]") {
  REQUIRE(SDL_WasInit(SDL_INIT_VIDEO) == 0);
  {
    const auto backend = create_or_skip();
    CHECK(SDL_WasInit(SDL_INIT_VIDEO) != 0);
  }
  CHECK(SDL_WasInit(SDL_INIT_VIDEO) == 0);
}

TEST_CASE("lifetime: a backend can be created again after the previous one is gone", "[desktop_display_backend]") {
  {
    const auto first = create_or_skip();
    present_two_tone(*first);
  }
  const auto second = create_or_skip();
  present_two_tone(*second);
  CHECK(second->size() == kPanel);
}

TEST_CASE("lifetime: two backends coexist and the survivor keeps rendering", "[desktop_display_backend]") {
  auto first = create_or_skip();
  const auto second = create_or_skip();
  CHECK(first->window() != second->window());
  present_two_tone(*first);
  present_two_tone(*second);

  first.backend.reset();
  CHECK(SDL_WasInit(SDL_INIT_VIDEO) != 0);  // the second still owns a reference
  present_two_tone(*second);
  CHECK(second->end_frame() == PresentResult::Failed);  // sanity: not inside a frame
}

}  // namespace
