// Render library tests: layout arithmetic, touch routing, gauge and alert classification, the
// settings and theme maths, and the drawing code run against a recording NanoVG backend.
//
// Drawing is verified without a GPU. NanoVG's core tessellates paths on the CPU and hands the
// results to a renderer through NVGparams, so a backend that only records what it is asked to
// fill and stroke sees the same geometry the GL backends would. Text needs font files; the
// repository ships none, so text is exercised with a system font where one exists and skipped
// otherwise. Everything here is tier 1: no display, no device.
//
// Sections are namespaces, one per header under test.

#include "audio/audio_state.hpp"
#include "platform/display_backend.hpp"
#include "platform/input_source.hpp"
#include "render/alerts.hpp"
#include "render/brand.hpp"
#include "render/camera_surface.hpp"
#include "render/canvas.hpp"
#include "render/console_scene.hpp"
#include "render/dock.hpp"
#include "render/draw.hpp"
#include "render/fonts.hpp"
#include "render/icons.hpp"
#include "render/layout.hpp"
#include "render/settings.hpp"
#include "render/sheet.hpp"
#include "render/status_tools.hpp"
#include "render/tabs.hpp"
#include "render/theme.hpp"
#include "render/touch_routing.hpp"
#include "render/units.hpp"
#include "render/widgets.hpp"
#include "telemetry/calibration.hpp"
#include "telemetry/frame.hpp"
#include "telemetry/sensor_source.hpp"
#include "telemetry/snapshot.hpp"
#include "vehicle/vehicle_commands.hpp"
#include "vehicle/vehicle_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

using c3::platform::Size;
using c3::platform::TouchEvent;
using c3::platform::TouchPhase;
using c3::render::Canvas;
using c3::render::Color;
using c3::render::Fonts;
using c3::render::Layout;
using c3::render::Rect;
using c3::render::rgb;
using c3::render::rgba;
using c3::render::Theme;
using c3::telemetry::Channel;
using c3::telemetry::Fault;
using c3::telemetry::FaultKind;
using c3::telemetry::Frame;
using c3::telemetry::LinkState;

CATCH_REGISTER_ENUM(TouchPhase, TouchPhase::Down, TouchPhase::Move, TouchPhase::Up, TouchPhase::Cancel)
CATCH_REGISTER_ENUM(c3::render::TouchTarget, c3::render::TouchTarget::Well, c3::render::TouchTarget::Ui)
CATCH_REGISTER_ENUM(c3::render::GaugeState, c3::render::GaugeState::Ok, c3::render::GaugeState::Warn,
                    c3::render::GaugeState::SenderFault, c3::render::GaugeState::Stale)
CATCH_REGISTER_ENUM(c3::render::AlertLevel, c3::render::AlertLevel::Alarm, c3::render::AlertLevel::Caution,
                    c3::render::AlertLevel::Neutral)
CATCH_REGISTER_ENUM(c3::render::Tab, c3::render::Tab::CarPlay, c3::render::Tab::Camera, c3::render::Tab::Settings,
                    c3::render::Tab::Count)
CATCH_REGISTER_ENUM(c3::render::Tool, c3::render::Tool::Theme, c3::render::Tool::Count)
CATCH_REGISTER_ENUM(c3::render::Page, c3::render::Page::None, c3::render::Page::Root, c3::render::Page::Audio,
                    c3::render::Page::Theme, c3::render::Page::Units, c3::render::Page::Calibration,
                    c3::render::Page::System, c3::render::Page::Count)
CATCH_REGISTER_ENUM(c3::render::CameraControl, c3::render::CameraControl::Guides, c3::render::CameraControl::Close)
CATCH_REGISTER_ENUM(c3::render::DockControl, c3::render::DockControl::BlowerOff, c3::render::DockControl::Blower1,
                    c3::render::DockControl::Blower2, c3::render::DockControl::Blower3,
                    c3::render::DockControl::Blower4, c3::render::DockControl::Ac, c3::render::DockControl::ModeVent,
                    c3::render::DockControl::ModeFloor, c3::render::DockControl::ModeDefrost,
                    c3::render::DockControl::Headlamps, c3::render::DockControl::Doors,
                    c3::render::DockControl::HighBeams, c3::render::DockControl::Count)
CATCH_REGISTER_ENUM(
    c3::render::SheetControl, c3::render::SheetControl::Close, c3::render::SheetControl::Back,
    c3::render::SheetControl::RowAudio, c3::render::SheetControl::RowTheme, c3::render::SheetControl::RowUnits,
    c3::render::SheetControl::RowCalibration, c3::render::SheetControl::RowSystem, c3::render::SheetControl::Volume,
    c3::render::SheetControl::Mute, c3::render::SheetControl::Balance, c3::render::SheetControl::Fader,
    c3::render::SheetControl::Sub, c3::render::SheetControl::Bass, c3::render::SheetControl::Mid,
    c3::render::SheetControl::Treble, c3::render::SheetControl::PresetFlat, c3::render::SheetControl::PresetRock,
    c3::render::SheetControl::PresetCruise, c3::render::SheetControl::PresetVoice, c3::render::SheetControl::ThemeDark,
    c3::render::SheetControl::ThemeLight, c3::render::SheetControl::ThemeAuto, c3::render::SheetControl::Swatch0,
    c3::render::SheetControl::Swatch1, c3::render::SheetControl::Swatch2, c3::render::SheetControl::Swatch3,
    c3::render::SheetControl::Swatch4, c3::render::SheetControl::Swatch5, c3::render::SheetControl::Swatch6,
    c3::render::SheetControl::Swatch7, c3::render::SheetControl::Swatch8, c3::render::SheetControl::Hue,
    c3::render::SheetControl::TempF, c3::render::SheetControl::TempC, c3::render::SheetControl::PressPsi,
    c3::render::SheetControl::PressBar, c3::render::SheetControl::Reset, c3::render::SheetControl::Count)
CATCH_REGISTER_ENUM(c3::render::TempUnit, c3::render::TempUnit::Fahrenheit, c3::render::TempUnit::Celsius)
CATCH_REGISTER_ENUM(c3::render::PressureUnit, c3::render::PressureUnit::Psi, c3::render::PressureUnit::Bar)
CATCH_REGISTER_ENUM(c3::render::ThemePreference, c3::render::ThemePreference::Dark, c3::render::ThemePreference::Light,
                    c3::render::ThemePreference::Auto)

namespace Catch {
template <>
struct StringMaker<Rect> {
  static std::string convert(const Rect& r) {
    std::array<char, 96> buf{};
    std::snprintf(buf.data(), buf.size(), "{x=%g y=%g w=%g h=%g}", static_cast<double>(r.x), static_cast<double>(r.y),
                  static_cast<double>(r.w), static_cast<double>(r.h));
    return buf.data();
  }
};
}  // namespace Catch

namespace {

// ===========================================================================
// shared fixtures
// ===========================================================================

constexpr Size kReferencePanel{.width = 1600, .height = 2560};  // the mockup, 800 × 1280 at 2×
constexpr Size kPanel{.width = 800, .height = 1280};            // spec §5.5 panel at 1×
constexpr Rect kReferenceWell{.x = 40.0F, .y = 100.0F, .w = 1520.0F, .h = 1392.0F};

[[nodiscard]] bool same_color(Color a, Color b, float eps = 1e-6F) noexcept {
  return std::fabs(a.r - b.r) <= eps && std::fabs(a.g - b.g) <= eps && std::fabs(a.b - b.b) <= eps &&
         std::fabs(a.a - b.a) <= eps;
}

[[nodiscard]] bool inside(const Rect& inner, const Rect& outer, float eps = 1e-3F) noexcept {
  return inner.x >= outer.x - eps && inner.y >= outer.y - eps && inner.right() <= outer.right() + eps &&
         inner.bottom() <= outer.bottom() + eps;
}

[[nodiscard]] bool overlaps(const Rect& a, const Rect& b) noexcept {
  return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

[[nodiscard]] bool whole_pixels(const Rect& r) noexcept {
  return r.x == std::round(r.x) && r.y == std::round(r.y) && r.w == std::round(r.w) && r.h == std::round(r.h);
}

void expect_rect(const Rect& actual, const Rect& expected, float eps = 1e-3F) {
  INFO("actual " << Catch::StringMaker<Rect>::convert(actual) << " expected "
                 << Catch::StringMaker<Rect>::convert(expected));
  CHECK_THAT(actual.x, WithinAbs(expected.x, eps));
  CHECK_THAT(actual.y, WithinAbs(expected.y, eps));
  CHECK_THAT(actual.w, WithinAbs(expected.w, eps));
  CHECK_THAT(actual.h, WithinAbs(expected.h, eps));
}

/// `r` scaled by `k` about the origin.
[[nodiscard]] constexpr Rect scaled(const Rect& r, float k) noexcept {
  return {.x = r.x * k, .y = r.y * k, .w = r.w * k, .h = r.h * k};
}

[[nodiscard]] constexpr Fault fault(FaultKind kind, Channel channel = Channel::Fuel) noexcept {
  return {.kind = kind, .channel = channel};
}

/// The spec §4.1 example frame with the given faults.
[[nodiscard]] Frame make_frame(std::initializer_list<Fault> faults = {}) {
  Frame f{.t_ms = 1000, .fuel_fraction = 0.62F, .oil_psi = 41.2F, .coolant_f = 196.4F, .voltage_v = 14.1F};
  for (const Fault& x : faults) {
    REQUIRE(f.faults.push_back(x));
  }
  return f;
}

// ---------------------------------------------------------------------------
// A NanoVG renderer that records what the core asks it to draw. Bounds are in device pixels,
// after the current transform, exactly as the GL backends would receive them.
// ---------------------------------------------------------------------------

struct FillCall {
  Rect bounds;
  Color inner;
  Color outer;
  std::optional<Rect> scissor;  // the clip in force, if any
};

struct StrokeCall {
  Rect bounds;  // of the stroke geometry, including its width and antialias fringe
  float width;
  Color inner;
};

class RecordingBackend {
public:
  std::vector<FillCall> fills;
  std::vector<StrokeCall> strokes;
  int triangle_calls = 0;  // text glyph quads

  [[nodiscard]] NVGparams params() noexcept {
    NVGparams p{};
    p.userPtr = this;
    p.edgeAntiAlias = 1;
    p.renderCreate = &on_create;
    p.renderCreateTexture = &on_create_texture;
    p.renderDeleteTexture = &on_delete_texture;
    p.renderUpdateTexture = &on_update_texture;
    p.renderGetTextureSize = &on_texture_size;
    p.renderViewport = &on_viewport;
    p.renderCancel = &on_nop;
    p.renderFlush = &on_nop;
    p.renderFill = &on_fill;
    p.renderStroke = &on_stroke;
    p.renderTriangles = &on_triangles;
    p.renderDelete = &on_nop;
    return p;
  }

  void clear() noexcept {
    fills.clear();
    strokes.clear();
    triangle_calls = 0;
  }

  [[nodiscard]] std::size_t fills_with(Color c) const noexcept {
    return static_cast<std::size_t>(
        std::count_if(fills.begin(), fills.end(), [&](const FillCall& f) { return same_color(f.inner, c); }));
  }

private:
  std::unordered_map<int, std::pair<int, int>> textures_;
  int next_texture_ = 1;

  [[nodiscard]] static RecordingBackend& self(void* uptr) noexcept { return *static_cast<RecordingBackend*>(uptr); }

  [[nodiscard]] static Rect from_bounds(const float* b) noexcept {
    return {.x = b[0], .y = b[1], .w = b[2] - b[0], .h = b[3] - b[1]};
  }

  [[nodiscard]] static std::optional<Rect> from_scissor(const NVGscissor* s) noexcept {
    if (s == nullptr || s->extent[0] < -0.5F) {
      return std::nullopt;  // NanoVG marks "no scissor" with a negative extent
    }
    return Rect{
        .x = s->xform[4] - s->extent[0], .y = s->xform[5] - s->extent[1], .w = 2 * s->extent[0], .h = 2 * s->extent[1]};
  }

  static int on_create(void*) { return 1; }
  static int on_create_texture(void* uptr, int, int w, int h, int, const unsigned char*) {
    RecordingBackend& b = self(uptr);
    const int id = b.next_texture_++;
    b.textures_[id] = {w, h};
    return id;
  }
  static int on_delete_texture(void* uptr, int id) { return self(uptr).textures_.erase(id) > 0 ? 1 : 0; }
  static int on_update_texture(void*, int, int, int, int, int, const unsigned char*) { return 1; }
  static int on_texture_size(void* uptr, int id, int* w, int* h) {
    const auto it = self(uptr).textures_.find(id);
    if (it == self(uptr).textures_.end()) {
      return 0;
    }
    *w = it->second.first;
    *h = it->second.second;
    return 1;
  }
  static void on_viewport(void*, float, float, float) {}
  static void on_nop(void*) {}
  static void on_fill(void* uptr, NVGpaint* paint, NVGcompositeOperationState, NVGscissor* scissor, float,
                      const float* bounds, const NVGpath*, int) {
    self(uptr).fills.push_back({.bounds = from_bounds(bounds),
                                .inner = paint->innerColor,
                                .outer = paint->outerColor,
                                .scissor = from_scissor(scissor)});
  }
  static void on_stroke(void* uptr, NVGpaint* paint, NVGcompositeOperationState, NVGscissor*, float, float width,
                        const NVGpath* paths, int npaths) {
    float x0 = 1e30F;
    float y0 = 1e30F;
    float x1 = -1e30F;
    float y1 = -1e30F;
    for (int i = 0; i < npaths; ++i) {
      for (int k = 0; k < paths[i].nstroke; ++k) {
        const NVGvertex& v = paths[i].stroke[k];
        x0 = std::min(x0, v.x);
        y0 = std::min(y0, v.y);
        x1 = std::max(x1, v.x);
        y1 = std::max(y1, v.y);
      }
    }
    self(uptr).strokes.push_back(
        {.bounds = {.x = x0, .y = y0, .w = x1 - x0, .h = y1 - y0}, .width = width, .inner = paint->innerColor});
  }
  static void on_triangles(void* uptr, NVGpaint*, NVGcompositeOperationState, NVGscissor*, const NVGvertex*, int,
                           float) {
    ++self(uptr).triangle_calls;
  }
};

/// A NanoVG context over the recording backend, inside an open frame for the 1× panel.
class NullCanvas {
public:
  NullCanvas() : params_(backend_.params()), vg_(nvgCreateInternal(&params_)) {
    REQUIRE(vg_ != nullptr);
    nvgBeginFrame(vg_, static_cast<float>(kPanel.width), static_cast<float>(kPanel.height), 1.0F);
  }
  ~NullCanvas() {
    nvgEndFrame(vg_);
    nvgDeleteInternal(vg_);
  }
  NullCanvas(const NullCanvas&) = delete;
  NullCanvas& operator=(const NullCanvas&) = delete;
  NullCanvas(NullCanvas&&) = delete;
  NullCanvas& operator=(NullCanvas&&) = delete;

  [[nodiscard]] Canvas* vg() const noexcept { return vg_; }
  [[nodiscard]] RecordingBackend& calls() noexcept { return backend_; }
  void clear() noexcept { backend_.clear(); }

  [[nodiscard]] std::array<float, 6> transform() const noexcept {
    std::array<float, 6> xf{};
    nvgCurrentTransform(vg_, xf.data());
    return xf;
  }
  [[nodiscard]] bool transform_is_identity() const noexcept {
    return transform() == std::array<float, 6>{1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F};
  }

private:
  RecordingBackend backend_;
  NVGparams params_;
  NVGcontext* vg_;
};

/// A unique temporary directory removed on destruction.
class ScratchDir {
public:
  ScratchDir() {
    static int counter = 0;
    path_ = std::filesystem::temp_directory_path() /
            ("c3_gui_rendering_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
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

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
  std::filesystem::path path_;
};

/// The files load_fonts() expects, in load order.
constexpr std::array<std::string_view, 6> kFontFiles = {"Inter-Regular.ttf",  "Inter-Medium.ttf",
                                                        "Inter-SemiBold.ttf", "Barlow-Light.ttf",
                                                        "Barlow-Medium.ttf",  "Barlow-SemiBold.ttf"};

/// Any TrueType file on this host that can stand in for the shipped fonts.
[[nodiscard]] std::optional<std::filesystem::path> system_font() {
  for (const char* candidate :
       {"/System/Library/Fonts/Supplemental/Arial.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf"}) {
    if (std::filesystem::exists(candidate)) {
      return std::filesystem::path(candidate);
    }
  }
  return std::nullopt;
}

/// Fills `dir` with the first `count` expected font files, all copies of `source`.
void populate_fonts(const ScratchDir& dir, const std::filesystem::path& source, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i) {
    std::filesystem::copy_file(source, dir.path() / std::string(kFontFiles[i]));
  }
}

// ===========================================================================
// layout.hpp
// ===========================================================================
namespace layout_tests {

using c3::render::kReferenceContentW;
using c3::render::kReferenceH;
using c3::render::kReferenceW;

TEST_CASE("Rect: edges and centre derive from origin and size", "[gui_rendering][layout]") {
  constexpr Rect r{.x = 10.0F, .y = 20.0F, .w = 30.0F, .h = 40.0F};
  STATIC_CHECK(r.right() == 40.0F);
  STATIC_CHECK(r.bottom() == 60.0F);
  STATIC_CHECK(r.cx() == 25.0F);
  STATIC_CHECK(r.cy() == 40.0F);
}

TEST_CASE("Rect: contains is half-open, so left/top edges are in and right/bottom edges are out",
          "[gui_rendering][layout]") {
  constexpr Rect r{.x = 10.0F, .y = 20.0F, .w = 30.0F, .h = 40.0F};
  STATIC_CHECK(r.contains(10.0F, 20.0F));
  STATIC_CHECK(r.contains(39.999F, 59.999F));
  STATIC_CHECK_FALSE(r.contains(40.0F, 30.0F));
  STATIC_CHECK_FALSE(r.contains(20.0F, 60.0F));
  STATIC_CHECK_FALSE(r.contains(9.999F, 30.0F));
  STATIC_CHECK_FALSE(r.contains(20.0F, 19.999F));
}

TEST_CASE("Rect: an empty rect contains nothing, not even its origin", "[gui_rendering][layout]") {
  constexpr Rect r{.x = 5.0F, .y = 5.0F};
  STATIC_CHECK_FALSE(r.contains(5.0F, 5.0F));
}

TEST_CASE("Layout: the reference panel reproduces the mockup's LAYOUT table exactly", "[gui_rendering][layout]") {
  const Layout l = Layout::compute(kReferencePanel);
  CHECK(l.panel == kReferencePanel);
  CHECK(l.scale == 1.0F);
  CHECK(l.status == Rect{.x = 40.0F, .y = 0.0F, .w = 1520.0F, .h = 88.0F});
  CHECK(l.well == kReferenceWell);
  CHECK(l.tabs == Rect{.x = 40.0F, .y = 1504.0F, .w = 1520.0F, .h = 64.0F});
  CHECK(l.cluster == Rect{.x = 40.0F, .y = 1584.0F, .w = 1520.0F, .h = 528.0F});
  CHECK(l.dock == Rect{.x = 40.0F, .y = 2128.0F, .w = 1520.0F, .h = 392.0F});
  CHECK(l.lever == Rect{.x = 0.0F, .y = 2524.0F, .w = 1600.0F, .h = 36.0F});
  CHECK(l.fuel == Rect{.x = 40.0F, .y = 1584.0F, .w = 496.0F, .h = 256.0F});
  CHECK(l.systems == Rect{.x = 40.0F, .y = 1856.0F, .w = 496.0F, .h = 256.0F});
  CHECK(l.oil == Rect{.x = 552.0F, .y = 1584.0F, .w = 496.0F, .h = 528.0F});
  CHECK(l.clt == Rect{.x = 1064.0F, .y = 1584.0F, .w = 496.0F, .h = 528.0F});
}

TEST_CASE("Layout: at 1x every region halves to whole pixels", "[gui_rendering][layout]") {
  const Layout l = Layout::compute(kPanel);
  CHECK(l.scale == 0.5F);
  CHECK(l.status == Rect{.x = 20.0F, .y = 0.0F, .w = 760.0F, .h = 44.0F});
  CHECK(l.tabs == Rect{.x = 20.0F, .y = 752.0F, .w = 760.0F, .h = 32.0F});
  CHECK(l.cluster == Rect{.x = 20.0F, .y = 792.0F, .w = 760.0F, .h = 264.0F});
  CHECK(l.dock == Rect{.x = 20.0F, .y = 1064.0F, .w = 760.0F, .h = 196.0F});
  CHECK(l.lever == Rect{.x = 0.0F, .y = 1262.0F, .w = 800.0F, .h = 18.0F});
  CHECK(l.fuel == Rect{.x = 20.0F, .y = 792.0F, .w = 248.0F, .h = 128.0F});
  CHECK(l.systems == Rect{.x = 20.0F, .y = 928.0F, .w = 248.0F, .h = 128.0F});
  CHECK(l.oil == Rect{.x = 276.0F, .y = 792.0F, .w = 248.0F, .h = 264.0F});
  CHECK(l.clt == Rect{.x = 532.0F, .y = 792.0F, .w = 248.0F, .h = 264.0F});
}

TEST_CASE("Layout: at 1x the well snaps down to multiples of 16 px and is centred", "[gui_rendering][layout]") {
  // Half the reference well would be 760 × 696, neither a multiple of 16. The video mode has
  // to be 16-aligned (spec §5.5), so the well gives up 8 px of width and 8 px of height.
  const Layout l = Layout::compute(kPanel);
  CHECK(l.well == Rect{.x = 24.0F, .y = 50.0F, .w = 752.0F, .h = 688.0F});
  CHECK(std::fmod(l.well.w, 16.0F) == 0.0F);
  CHECK(std::fmod(l.well.h, 16.0F) == 0.0F);
  CHECK_THAT(l.well.cx(), WithinAbs(l.status.cx(), 1e-3));
  CHECK(l.well.bottom() <= l.tabs.y);
}

TEST_CASE("Layout: a panel of another aspect keeps the regions ordered, aligned and whole-pixel",
          "[gui_rendering][layout]") {
  for (const Size panel :
       {Size{.width = 1668, .height = 2388}, Size{.width = 1080, .height = 1920}, Size{.width = 600, .height = 1024}}) {
    INFO("panel " << panel.width << " x " << panel.height);
    const Layout l = Layout::compute(panel);
    const auto w = static_cast<float>(panel.width);
    CHECK_THAT(l.scale, WithinRel(w / kReferenceW, 1e-6F));

    for (const Rect* r :
         {&l.status, &l.well, &l.tabs, &l.cluster, &l.fuel, &l.systems, &l.oil, &l.clt, &l.dock, &l.lever}) {
      CHECK(whole_pixels(*r));
      CHECK(r->w > 0.0F);
      CHECK(r->h > 0.0F);
      CHECK(inside(*r, Rect{.x = 0.0F, .y = 0.0F, .w = w, .h = static_cast<float>(panel.height)}));
    }

    // top to bottom, no overlap
    CHECK(l.status.bottom() <= l.well.y);
    CHECK(l.well.bottom() <= l.tabs.y);
    CHECK(l.tabs.bottom() <= l.cluster.y);
    CHECK(l.cluster.bottom() <= l.dock.y);
    CHECK(l.dock.bottom() <= l.lever.y);

    // the well is 16-aligned and centred in the content column
    CHECK(std::fmod(l.well.w, 16.0F) == 0.0F);
    CHECK(std::fmod(l.well.h, 16.0F) == 0.0F);
    CHECK_THAT(l.well.cx(), WithinAbs(l.status.cx(), 0.5));
    CHECK(l.well.w <= l.status.w);

    // the four tiles partition the cluster: left column stacked, arcs full height
    CHECK(l.fuel.x == l.cluster.x);
    CHECK(l.fuel.y == l.cluster.y);
    CHECK(l.systems.x == l.cluster.x);
    CHECK(l.systems.y > l.fuel.bottom());
    CHECK(l.systems.bottom() == l.cluster.bottom());
    CHECK(l.oil.h == l.cluster.h);
    CHECK(l.clt.h == l.cluster.h);
    CHECK(l.oil.x > l.fuel.right());
    CHECK(l.clt.x > l.oil.right());
    CHECK_THAT(l.clt.right(), WithinAbs(l.cluster.right(), 2.0));  // three rounded tile widths
    CHECK(l.systems.bottom() == l.oil.bottom());
  }
}

TEST_CASE("Layout: a degenerate panel yields empty rects instead of garbage", "[gui_rendering][layout]") {
  for (const Size panel :
       {Size{.width = 0, .height = 1280}, Size{.width = 800, .height = 0}, Size{.width = -1, .height = -1}}) {
    const Layout l = Layout::compute(panel);
    CHECK(l.panel == panel);
    CHECK(l.scale == 1.0F);
    CHECK(l.well == Rect{});
    CHECK(l.dock == Rect{});
    CHECK(l.status == Rect{});
  }
}

TEST_CASE("Layout: reference constants agree with each other", "[gui_rendering][layout]") {
  STATIC_CHECK(kReferenceW == 1600.0F);
  STATIC_CHECK(kReferenceH == 2560.0F);
  STATIC_CHECK(kReferenceContentW == 1520.0F);
  CHECK(std::fmod(kReferenceContentW, 16.0F) == 0.0F);  // the well can be 16-aligned at full width
}

}  // namespace layout_tests

// ===========================================================================
// units.hpp
// ===========================================================================
namespace units_tests {

using c3::render::kAllPressureUnits;
using c3::render::kAllTempUnits;
using c3::render::pressure_scale;
using c3::render::PressureUnit;
using c3::render::temp_scale;
using c3::render::TempUnit;
using c3::render::to_display_pressure;
using c3::render::to_display_temp;
using c3::render::to_string;
using c3::render::Units;
using c3::render::UnitScale;

TEST_CASE("units: to_string gives the config spelling", "[gui_rendering][units]") {
  STATIC_CHECK(to_string(TempUnit::Fahrenheit) == "F");
  STATIC_CHECK(to_string(TempUnit::Celsius) == "C");
  STATIC_CHECK(to_string(PressureUnit::Psi) == "psi");
  STATIC_CHECK(to_string(PressureUnit::Bar) == "bar");
}

TEST_CASE("units: Fahrenheit and psi pass through unchanged", "[gui_rendering][units]") {
  STATIC_CHECK(to_display_temp(TempUnit::Fahrenheit, 196.4F) == 196.4F);
  STATIC_CHECK(to_display_pressure(PressureUnit::Psi, 41.2F) == 41.2F);
}

TEST_CASE("units: Celsius and bar conversions use the standard factors", "[gui_rendering][units]") {
  CHECK_THAT(to_display_temp(TempUnit::Celsius, 212.0F), WithinAbs(100.0, 1e-4));
  CHECK_THAT(to_display_temp(TempUnit::Celsius, 32.0F), WithinAbs(0.0, 1e-4));
  CHECK_THAT(to_display_temp(TempUnit::Celsius, -40.0F), WithinAbs(-40.0, 1e-4));
  CHECK_THAT(to_display_pressure(PressureUnit::Bar, 14.5038F), WithinAbs(1.0, 1e-4));
  CHECK_THAT(to_display_pressure(PressureUnit::Bar, 0.0F), WithinAbs(0.0, 1e-6));
}

TEST_CASE("units: the arc scales match the mockup tables, including the red zones", "[gui_rendering][units]") {
  constexpr UnitScale f = temp_scale(TempUnit::Fahrenheit);
  STATIC_CHECK(f.unit ==
               "\xC2\xB0"
               "F");
  STATIC_CHECK(f.min == 100.0F);
  STATIC_CHECK(f.max == 260.0F);
  STATIC_CHECK(f.major == 40.0F);
  STATIC_CHECK(f.decimals == 0);
  STATIC_CHECK(f.zone_lo == 235.0F);
  STATIC_CHECK(f.zone_hi == 260.0F);

  constexpr UnitScale c = temp_scale(TempUnit::Celsius);
  STATIC_CHECK(c.unit ==
               "\xC2\xB0"
               "C");
  STATIC_CHECK(c.min == 40.0F);
  STATIC_CHECK(c.max == 120.0F);
  STATIC_CHECK(c.major == 20.0F);
  STATIC_CHECK(c.zone_lo == 113.0F);
  STATIC_CHECK(c.zone_hi == 120.0F);

  constexpr UnitScale psi = pressure_scale(PressureUnit::Psi);
  STATIC_CHECK(psi.unit == "psi");
  STATIC_CHECK(psi.min == 0.0F);
  STATIC_CHECK(psi.max == 80.0F);
  STATIC_CHECK(psi.major == 20.0F);
  STATIC_CHECK(psi.decimals == 0);
  STATIC_CHECK(psi.zone_lo == 0.0F);
  STATIC_CHECK(psi.zone_hi == 12.0F);

  constexpr UnitScale bar = pressure_scale(PressureUnit::Bar);
  STATIC_CHECK(bar.unit == "bar");
  STATIC_CHECK(bar.max == 6.0F);
  STATIC_CHECK(bar.major == 1.5F);
  STATIC_CHECK(bar.decimals == 1);
  STATIC_CHECK(bar.zone_hi == 0.8F);
}

TEST_CASE("units: the Celsius and bar red zones are the Fahrenheit and psi ones converted", "[gui_rendering][units]") {
  // The zones mirror the MCU's thresholds (235 °F, 12 psi) in each unit; the picture must not
  // move the redline when the unit changes.
  CHECK_THAT(to_display_temp(TempUnit::Celsius, temp_scale(TempUnit::Fahrenheit).zone_lo),
             WithinAbs(temp_scale(TempUnit::Celsius).zone_lo, 0.5));
  CHECK_THAT(to_display_pressure(PressureUnit::Bar, pressure_scale(PressureUnit::Psi).zone_hi),
             WithinAbs(pressure_scale(PressureUnit::Bar).zone_hi, 0.05));
}

TEST_CASE("units: every scale's zone lies on the scale and the tick step divides it", "[gui_rendering][units]") {
  for (const TempUnit u : kAllTempUnits) {
    const UnitScale s = temp_scale(u);
    CHECK(s.min <= s.zone_lo);
    CHECK(s.zone_lo < s.zone_hi);
    CHECK(s.zone_hi <= s.max);
    CHECK_THAT(std::fmod(s.max - s.min, s.major), WithinAbs(0.0, 1e-4));
  }
  for (const PressureUnit u : kAllPressureUnits) {
    const UnitScale s = pressure_scale(u);
    CHECK(s.min <= s.zone_lo);
    CHECK(s.zone_lo < s.zone_hi);
    CHECK(s.zone_hi <= s.max);
    CHECK_THAT(std::fmod(s.max - s.min, s.major), WithinAbs(0.0, 1e-4));
  }
}

TEST_CASE("units: Units defaults to the MCU's own units and compares by value", "[gui_rendering][units]") {
  constexpr Units a{};
  STATIC_CHECK(a.temp == TempUnit::Fahrenheit);
  STATIC_CHECK(a.pressure == PressureUnit::Psi);
  STATIC_CHECK(a == Units{.temp = TempUnit::Fahrenheit, .pressure = PressureUnit::Psi});
  STATIC_CHECK(a != Units{.temp = TempUnit::Celsius, .pressure = PressureUnit::Psi});
}

}  // namespace units_tests

// ===========================================================================
// theme.hpp
// ===========================================================================
namespace theme_tests {

using c3::render::luminance;

TEST_CASE("theme: rgb unpacks 0xRRGGBB into unit floats with full alpha", "[gui_rendering][theme]") {
  const Color c = rgb(0xFF6A2B);
  CHECK_THAT(c.r, WithinAbs(1.0, 1e-6));
  CHECK_THAT(c.g, WithinAbs(106.0 / 255.0, 1e-6));
  CHECK_THAT(c.b, WithinAbs(43.0 / 255.0, 1e-6));
  CHECK(c.a == 1.0F);
}

TEST_CASE("theme: rgba carries the alpha and ignores bits above the colour", "[gui_rendering][theme]") {
  const Color c = rgba(0xFF000000U | 0x0000FFU, 0.25F);
  CHECK(c.r == 0.0F);
  CHECK(c.g == 0.0F);
  CHECK(c.b == 1.0F);
  CHECK(c.a == 0.25F);
}

TEST_CASE("theme: luminance is WCAG relative luminance", "[gui_rendering][theme]") {
  CHECK_THAT(luminance(rgb(0xFFFFFF)), WithinAbs(1.0, 1e-5));
  CHECK_THAT(luminance(rgb(0x000000)), WithinAbs(0.0, 1e-6));
  CHECK_THAT(luminance(rgb(0x808080)), WithinAbs(0.2159, 1e-3));  // sRGB mid grey
  CHECK_THAT(luminance(rgb(0x00FF00)), WithinAbs(0.7152, 1e-5));  // the green coefficient
  CHECK(luminance(rgb(0xFF0000)) < luminance(rgb(0x00FF00)));
}

TEST_CASE("theme: set_accent picks a dark ink on light accents and a white ink on dark ones",
          "[gui_rendering][theme]") {
  Theme t = Theme::dark();
  t.set_accent(rgb(0xD4D4D8));  // Silver
  CHECK(same_color(t.ember, rgb(0xD4D4D8)));
  CHECK(same_color(t.ember_ink, rgb(0x141210)));

  t.set_accent(rgb(0x6366F1));  // Indigo, luminance ≈ 0.19
  CHECK(same_color(t.ember, rgb(0x6366F1)));
  CHECK(same_color(t.ember_ink, rgb(0xFFFFFF)));

  t.set_accent(rgb(0xFF6A2B));  // Ember, luminance ≈ 0.32
  CHECK(same_color(t.ember_ink, rgb(0x141210)));
}

TEST_CASE("theme: set_accent touches only the accent pair", "[gui_rendering][theme]") {
  const Theme before = Theme::light();
  Theme after = before;
  after.set_accent(rgb(0x2DD4BF));
  CHECK(same_color(after.bg, before.bg));
  CHECK(same_color(after.ink, before.ink));
  CHECK(same_color(after.alarm, before.alarm));
  CHECK(same_color(after.amber, before.amber));
  CHECK(same_color(after.fill1, before.fill1));
  CHECK_FALSE(same_color(after.ember, before.ember));
}

TEST_CASE("theme: dark and light keep ink readable against their background", "[gui_rendering][theme]") {
  const Theme d = Theme::dark();
  const Theme l = Theme::light();
  CHECK(luminance(d.ink) > luminance(d.bg) + 0.8F);
  CHECK(luminance(l.bg) > luminance(l.ink) + 0.8F);
  CHECK(luminance(d.ink) > luminance(d.ink2));
  CHECK(luminance(d.ink2) > luminance(d.ink3));
  CHECK(luminance(l.ink) < luminance(l.ink2));
  CHECK(luminance(l.ink2) < luminance(l.ink3));
}

TEST_CASE("theme: what must not change with the theme does not", "[gui_rendering][theme]") {
  const Theme d = Theme::dark();
  const Theme l = Theme::light();
  CHECK(same_color(d.ember, l.ember));      // the default accent
  CHECK(same_color(d.ok, l.ok));            // link dot
  CHECK(same_color(d.well_bg, l.well_bg));  // the phone owns the well's look
  // warnings stay red and amber on both
  CHECK(d.alarm.r > 0.85F);
  CHECK(l.alarm.r > 0.85F);
  CHECK(d.amber.r > 0.85F);
  CHECK(d.amber.g > 0.6F);
  CHECK(l.amber.r > 0.85F);
}

}  // namespace theme_tests

// ===========================================================================
// settings.hpp
// ===========================================================================
namespace settings_tests {

using c3::render::accent_from_hue;
using c3::render::effective_dark;
using c3::render::hue_of;
using c3::render::kAllThemePreferences;
using c3::render::kSwatches;
using c3::render::make_theme;
using c3::render::Settings;
using c3::render::swatch_index;
using c3::render::ThemePreference;
using c3::render::to_string;

TEST_CASE("settings: to_string spells the config values", "[gui_rendering][settings]") {
  STATIC_CHECK(to_string(ThemePreference::Dark) == "dark");
  STATIC_CHECK(to_string(ThemePreference::Light) == "light");
  STATIC_CHECK(to_string(ThemePreference::Auto) == "auto");
  STATIC_CHECK(kAllThemePreferences.size() == 3);
}

TEST_CASE("settings: effective_dark resolves Auto from the headlamp relay only", "[gui_rendering][settings]") {
  STATIC_CHECK(effective_dark(Settings{.theme = ThemePreference::Dark}, false));
  STATIC_CHECK(effective_dark(Settings{.theme = ThemePreference::Dark}, true));
  STATIC_CHECK_FALSE(effective_dark(Settings{.theme = ThemePreference::Light}, false));
  STATIC_CHECK_FALSE(effective_dark(Settings{.theme = ThemePreference::Light}, true));
  STATIC_CHECK_FALSE(effective_dark(Settings{.theme = ThemePreference::Auto}, false));
  STATIC_CHECK(effective_dark(Settings{.theme = ThemePreference::Auto}, true));
}

TEST_CASE("settings: make_theme picks the palette and applies the accent", "[gui_rendering][settings]") {
  const Settings auto_mode{.theme = ThemePreference::Auto, .accent = 0x2DD4BF};
  const Theme night = make_theme(auto_mode, true);
  const Theme day = make_theme(auto_mode, false);
  CHECK(same_color(night.bg, Theme::dark().bg));
  CHECK(same_color(day.bg, Theme::light().bg));
  CHECK(same_color(night.ember, rgb(0x2DD4BF)));
  CHECK(same_color(day.ember, rgb(0x2DD4BF)));
  CHECK(same_color(night.ember_ink, rgb(0x141210)));  // Teal is light enough for the dark ink
}

TEST_CASE("settings: accent_from_hue produces the fixed-saturation, fixed-lightness HSL colour",
          "[gui_rendering][settings]") {
  // HSL(h, 92 %, 58 %): the dominant channel is 0.58 + 0.3864 → 246, the others 0.58 − 0.3864 → 49.
  CHECK(accent_from_hue(0.0F) == 0xF63131);
  CHECK(accent_from_hue(120.0F) == 0x31F631);
  CHECK(accent_from_hue(240.0F) == 0x3131F6);
  CHECK(accent_from_hue(60.0F) == 0xF6F631);
}

TEST_CASE("settings: accent_from_hue wraps angles outside 0..360", "[gui_rendering][settings]") {
  CHECK(accent_from_hue(360.0F) == accent_from_hue(0.0F));
  CHECK(accent_from_hue(-120.0F) == accent_from_hue(240.0F));
  CHECK(accent_from_hue(480.0F) == accent_from_hue(120.0F));
}

TEST_CASE("settings: hue_of recovers the hue of pure colours and reads greys as 0", "[gui_rendering][settings]") {
  CHECK_THAT(hue_of(0xFF0000), WithinAbs(0.0, 1e-4));
  CHECK_THAT(hue_of(0x00FF00), WithinAbs(120.0, 1e-4));
  CHECK_THAT(hue_of(0x0000FF), WithinAbs(240.0, 1e-4));
  CHECK_THAT(hue_of(0xFFFF00), WithinAbs(60.0, 1e-4));
  CHECK_THAT(hue_of(0xFF00FF), WithinAbs(300.0, 1e-4));
  CHECK_THAT(hue_of(0xFF6A2B), WithinAbs(17.83, 0.05));  // Ember
  CHECK(hue_of(0x000000) == 0.0F);
  CHECK(hue_of(0xFFFFFF) == 0.0F);
  CHECK(hue_of(0x808080) == 0.0F);
}

TEST_CASE("settings: hue_of inverts accent_from_hue to within 8-bit rounding", "[gui_rendering][settings]") {
  for (const float hue : {0.0F, 15.0F, 75.0F, 120.0F, 199.0F, 250.0F, 300.0F, 345.0F}) {
    INFO("hue " << hue);
    CHECK_THAT(hue_of(accent_from_hue(hue)), WithinAbs(hue, 1.0));
  }
}

TEST_CASE("settings: swatch_index finds each swatch and nothing else", "[gui_rendering][settings]") {
  for (std::size_t i = 0; i < kSwatches.size(); ++i) {
    INFO(kSwatches[i].name);
    CHECK(swatch_index(kSwatches[i].rgb) == i);
  }
  CHECK_FALSE(swatch_index(0x123456).has_value());
  CHECK_FALSE(swatch_index(accent_from_hue(200.0F)).has_value());
}

TEST_CASE("settings: the swatch table has nine distinct, named colours and Ember first", "[gui_rendering][settings]") {
  STATIC_CHECK(kSwatches.size() == 9);
  STATIC_CHECK(kSwatches[0].name == "Ember");
  STATIC_CHECK(kSwatches[0].rgb == 0xFF6A2B);
  for (std::size_t i = 0; i < kSwatches.size(); ++i) {
    CHECK_FALSE(kSwatches[i].name.empty());
    for (std::size_t j = i + 1; j < kSwatches.size(); ++j) {
      CHECK(kSwatches[i].rgb != kSwatches[j].rgb);
      CHECK(kSwatches[i].name != kSwatches[j].name);
    }
  }
}

TEST_CASE("settings: defaults are dark, Ember and MCU units", "[gui_rendering][settings]") {
  constexpr Settings s{};
  STATIC_CHECK(s.theme == ThemePreference::Dark);
  STATIC_CHECK(s.accent == kSwatches[0].rgb);
  STATIC_CHECK(s.units == c3::render::Units{});
  STATIC_CHECK(s == Settings{});
  STATIC_CHECK(s != Settings{.accent = 0x000001});
}

}  // namespace settings_tests

// ===========================================================================
// touch_routing.hpp
// ===========================================================================
namespace touch_routing_tests {

using c3::render::route_touch;
using c3::render::RoutedGesture;
using c3::render::RoutedTouch;
using c3::render::TouchRouter;
using c3::render::TouchTarget;

constexpr Layout kLayout{.panel = kReferencePanel, .scale = 1.0F, .well = kReferenceWell};

[[nodiscard]] constexpr TouchEvent ev(TouchPhase phase, float x, float y, std::int32_t id = 0) noexcept {
  return {.phase = phase, .id = id, .x = x, .y = y};
}

TEST_CASE("route_touch: a point inside the well goes to CarPlay in well-local coordinates",
          "[gui_rendering][touch_routing]") {
  constexpr RoutedTouch r = route_touch(kLayout, 140.0F, 350.0F);
  STATIC_CHECK(r.target == TouchTarget::Well);
  STATIC_CHECK(r.x == 100.0F);
  STATIC_CHECK(r.y == 250.0F);
}

TEST_CASE("route_touch: a point outside the well is ours in panel coordinates", "[gui_rendering][touch_routing]") {
  constexpr RoutedTouch r = route_touch(kLayout, 800.0F, 2300.0F);  // the dock
  STATIC_CHECK(r.target == TouchTarget::Ui);
  STATIC_CHECK(r.x == 800.0F);
  STATIC_CHECK(r.y == 2300.0F);
}

TEST_CASE("route_touch: the well boundary is half-open", "[gui_rendering][touch_routing]") {
  // left and top edges belong to CarPlay
  STATIC_CHECK(route_touch(kLayout, kReferenceWell.x, kReferenceWell.y).target == TouchTarget::Well);
  STATIC_CHECK(route_touch(kLayout, kReferenceWell.x, kReferenceWell.y).x == 0.0F);
  STATIC_CHECK(route_touch(kLayout, kReferenceWell.x, kReferenceWell.y).y == 0.0F);
  // right and bottom edges are ours
  STATIC_CHECK(route_touch(kLayout, kReferenceWell.right(), 500.0F).target == TouchTarget::Ui);
  STATIC_CHECK(route_touch(kLayout, 500.0F, kReferenceWell.bottom()).target == TouchTarget::Ui);
  // one pixel inside those edges is CarPlay's
  STATIC_CHECK(route_touch(kLayout, kReferenceWell.right() - 1.0F, 500.0F).target == TouchTarget::Well);
  STATIC_CHECK(route_touch(kLayout, 500.0F, kReferenceWell.bottom() - 1.0F).target == TouchTarget::Well);
}

TEST_CASE("route_touch: nothing is forwarded while another surface occupies the well",
          "[gui_rendering][touch_routing]") {
  constexpr RoutedTouch r = route_touch(kLayout, 140.0F, 350.0F, false);
  STATIC_CHECK(r.target == TouchTarget::Ui);
  STATIC_CHECK(r.x == 140.0F);
  STATIC_CHECK(r.y == 350.0F);
}

TEST_CASE("TouchRouter: a gesture that starts in the well stays CarPlay's and is clamped to it",
          "[gui_rendering][touch_routing]") {
  TouchRouter router;
  const auto down = router.route(kLayout, ev(TouchPhase::Down, 140.0F, 350.0F), true);
  REQUIRE(down.has_value());
  CHECK(down->target == TouchTarget::Well);
  CHECK(down->event.phase == TouchPhase::Down);
  CHECK(down->event.x == 100.0F);
  CHECK(down->event.y == 250.0F);

  // drag out past the right and bottom edges: still CarPlay's, pinned to the last pixel
  const auto moved = router.route(kLayout, ev(TouchPhase::Move, 1700.0F, 1600.0F), true);
  REQUIRE(moved.has_value());
  CHECK(moved->target == TouchTarget::Well);
  CHECK(moved->event.phase == TouchPhase::Move);
  CHECK(moved->event.x == kReferenceWell.w - 1.0F);
  CHECK(moved->event.y == kReferenceWell.h - 1.0F);

  // and past the left and top edges: pinned to zero
  const auto moved2 = router.route(kLayout, ev(TouchPhase::Move, 0.0F, 0.0F), true);
  REQUIRE(moved2.has_value());
  CHECK(moved2->event.x == 0.0F);
  CHECK(moved2->event.y == 0.0F);

  const auto up = router.route(kLayout, ev(TouchPhase::Up, 800.0F, 2300.0F), true);
  REQUIRE(up.has_value());
  CHECK(up->target == TouchTarget::Well);
  CHECK(up->event.phase == TouchPhase::Up);

  // the gesture is over: a Move with no Down belongs to nobody
  CHECK_FALSE(router.route(kLayout, ev(TouchPhase::Move, 140.0F, 350.0F), true).has_value());
}

TEST_CASE("TouchRouter: a gesture that starts on the UI stays ours even when it crosses into the well",
          "[gui_rendering][touch_routing]") {
  TouchRouter router;
  const auto down = router.route(kLayout, ev(TouchPhase::Down, 800.0F, 2300.0F), true);
  REQUIRE(down.has_value());
  CHECK(down->target == TouchTarget::Ui);

  const auto moved = router.route(kLayout, ev(TouchPhase::Move, 140.0F, 350.0F), true);
  REQUIRE(moved.has_value());
  CHECK(moved->target == TouchTarget::Ui);
  CHECK(moved->event.x == 140.0F);  // panel coordinates, never well-local
  CHECK(moved->event.y == 350.0F);

  const auto up = router.route(kLayout, ev(TouchPhase::Up, 140.0F, 350.0F), true);
  REQUIRE(up.has_value());
  CHECK(up->target == TouchTarget::Ui);
}

TEST_CASE("TouchRouter: a second finger is ignored while one is down", "[gui_rendering][touch_routing]") {
  TouchRouter router;
  REQUIRE(router.route(kLayout, ev(TouchPhase::Down, 140.0F, 350.0F, 1), true).has_value());

  CHECK_FALSE(router.route(kLayout, ev(TouchPhase::Down, 800.0F, 2300.0F, 2), true).has_value());
  CHECK_FALSE(router.route(kLayout, ev(TouchPhase::Move, 810.0F, 2300.0F, 2), true).has_value());
  CHECK_FALSE(router.route(kLayout, ev(TouchPhase::Up, 810.0F, 2300.0F, 2), true).has_value());

  // the first finger is unaffected and keeps its id
  const auto up = router.route(kLayout, ev(TouchPhase::Up, 150.0F, 350.0F, 1), true);
  REQUIRE(up.has_value());
  CHECK(up->event.id == 1);
  CHECK(up->target == TouchTarget::Well);

  // now the second finger may start a gesture
  const auto down2 = router.route(kLayout, ev(TouchPhase::Down, 800.0F, 2300.0F, 2), true);
  REQUIRE(down2.has_value());
  CHECK(down2->target == TouchTarget::Ui);
  CHECK(down2->event.id == 2);
}

TEST_CASE("TouchRouter: Cancel ends the gesture like Up", "[gui_rendering][touch_routing]") {
  TouchRouter router;
  REQUIRE(router.route(kLayout, ev(TouchPhase::Down, 140.0F, 350.0F), true).has_value());
  const auto cancel = router.route(kLayout, ev(TouchPhase::Cancel, 140.0F, 350.0F), true);
  REQUIRE(cancel.has_value());
  CHECK(cancel->event.phase == TouchPhase::Cancel);
  CHECK_FALSE(router.route(kLayout, ev(TouchPhase::Up, 140.0F, 350.0F), true).has_value());
}

TEST_CASE("TouchRouter: a surface takeover cancels the gesture and swallows its release",
          "[gui_rendering][touch_routing]") {
  TouchRouter router;
  CHECK_FALSE(router.set_surface(1).has_value());  // nothing in progress: nothing to cancel

  REQUIRE(router.route(kLayout, ev(TouchPhase::Down, 140.0F, 350.0F, 7), true).has_value());
  REQUIRE(router.route(kLayout, ev(TouchPhase::Move, 160.0F, 360.0F, 7), true).has_value());

  const auto cancelled = router.set_surface(2);
  REQUIRE(cancelled.has_value());
  CHECK(cancelled->target == TouchTarget::Well);
  CHECK(cancelled->event.phase == TouchPhase::Cancel);
  CHECK(cancelled->event.id == 7);
  CHECK(cancelled->event.x == 120.0F);  // the last routed position, well-local
  CHECK(cancelled->event.y == 260.0F);

  // the finger's own release has no recipient any more
  CHECK_FALSE(router.route(kLayout, ev(TouchPhase::Move, 170.0F, 360.0F, 7), true).has_value());
  CHECK_FALSE(router.route(kLayout, ev(TouchPhase::Up, 170.0F, 360.0F, 7), true).has_value());

  // a fresh Down goes to the new occupant: with CarPlay inactive, the well is ours
  const auto down = router.route(kLayout, ev(TouchPhase::Down, 140.0F, 350.0F, 7), false);
  REQUIRE(down.has_value());
  CHECK(down->target == TouchTarget::Ui);
}

TEST_CASE("TouchRouter: setting the same surface again is a no-op mid-gesture", "[gui_rendering][touch_routing]") {
  TouchRouter router;
  CHECK_FALSE(router.set_surface(3).has_value());
  REQUIRE(router.route(kLayout, ev(TouchPhase::Down, 140.0F, 350.0F), true).has_value());
  CHECK_FALSE(router.set_surface(3).has_value());
  const auto up = router.route(kLayout, ev(TouchPhase::Up, 140.0F, 350.0F), true);
  REQUIRE(up.has_value());
  CHECK(up->target == TouchTarget::Well);
}

TEST_CASE("TouchRouter: the recipient is decided at Down from the surface state at that moment",
          "[gui_rendering][touch_routing]") {
  TouchRouter router;
  // the camera owns the well: a touch inside it is ours
  const auto down = router.route(kLayout, ev(TouchPhase::Down, 140.0F, 350.0F), false);
  REQUIRE(down.has_value());
  CHECK(down->target == TouchTarget::Ui);
  CHECK(down->event.x == 140.0F);
  // CarPlay coming back mid-gesture does not steal it
  const auto up = router.route(kLayout, ev(TouchPhase::Up, 140.0F, 350.0F), true);
  REQUIRE(up.has_value());
  CHECK(up->target == TouchTarget::Ui);
}

}  // namespace touch_routing_tests

// ===========================================================================
// tabs.hpp
// ===========================================================================
namespace tabs_tests {

using c3::render::kTabCount;
using c3::render::Tab;
using c3::render::TabsLayout;
using c3::render::TabsModel;
using c3::render::to_string;

constexpr Rect kTabsBar{.x = 40.0F, .y = 1504.0F, .w = 1520.0F, .h = 64.0F};

TEST_CASE("tabs: to_string names the three surfaces", "[gui_rendering][tabs]") {
  STATIC_CHECK(kTabCount == 3);
  STATIC_CHECK(to_string(Tab::CarPlay) == "carplay");
  STATIC_CHECK(to_string(Tab::Camera) == "camera");
  STATIC_CHECK(to_string(Tab::Settings) == "settings");
  STATIC_CHECK(to_string(Tab::Count) == "?");
}

TEST_CASE("tabs: the reference bar centres a 606-unit pill of three fixed-width buttons", "[gui_rendering][tabs]") {
  const TabsLayout tl = TabsLayout::compute(kTabsBar);
  CHECK(tl.scale == 1.0F);
  expect_rect(tl.pill, {.x = 497.0F, .y = 1504.0F, .w = 606.0F, .h = 64.0F});
  expect_rect(tl.rect(Tab::CarPlay), {.x = 503.0F, .y = 1510.0F, .w = 188.0F, .h = 52.0F});
  expect_rect(tl.rect(Tab::Camera), {.x = 697.0F, .y = 1510.0F, .w = 204.0F, .h = 52.0F});
  expect_rect(tl.rect(Tab::Settings), {.x = 907.0F, .y = 1510.0F, .w = 190.0F, .h = 52.0F});
  CHECK_THAT(tl.pill.cx(), WithinAbs(kTabsBar.cx(), 1e-3));
}

TEST_CASE("tabs: buttons sit inside the pill with 6-unit padding and gaps", "[gui_rendering][tabs]") {
  const TabsLayout tl = TabsLayout::compute(kTabsBar);
  for (std::size_t i = 0; i < kTabCount; ++i) {
    CHECK(inside(tl.buttons[i], tl.pill));
    CHECK(tl.buttons[i].y == tl.pill.y + 6.0F);
    CHECK(tl.buttons[i].bottom() == tl.pill.bottom() - 6.0F);
  }
  CHECK(tl.buttons[0].x == tl.pill.x + 6.0F);
  CHECK_THAT(tl.buttons[1].x - tl.buttons[0].right(), WithinAbs(6.0, 1e-3));
  CHECK_THAT(tl.buttons[2].x - tl.buttons[1].right(), WithinAbs(6.0, 1e-3));
  CHECK_THAT(tl.pill.right() - tl.buttons[2].right(), WithinAbs(6.0, 1e-3));
}

TEST_CASE("tabs: the layout scales with the bar height", "[gui_rendering][tabs]") {
  const TabsLayout ref = TabsLayout::compute(kTabsBar);
  const TabsLayout half = TabsLayout::compute(scaled(kTabsBar, 0.5F));
  CHECK(half.scale == 0.5F);
  expect_rect(half.pill, scaled(ref.pill, 0.5F));
  for (std::size_t i = 0; i < kTabCount; ++i) {
    expect_rect(half.buttons[i], scaled(ref.buttons[i], 0.5F));
  }
}

TEST_CASE("tabs: hit_test returns the button under the point and nothing for padding or gaps",
          "[gui_rendering][tabs]") {
  const TabsLayout tl = TabsLayout::compute(kTabsBar);
  for (std::size_t i = 0; i < kTabCount; ++i) {
    const auto tab = static_cast<Tab>(i);
    const Rect& r = tl.rect(tab);
    CHECK(tl.hit_test(r.cx(), r.cy()) == tab);
    CHECK(tl.hit_test(r.x, r.y) == tab);                       // top-left corner is inside
    CHECK_FALSE(tl.hit_test(r.right(), r.cy()).has_value());   // right edge is the gap
    CHECK_FALSE(tl.hit_test(r.cx(), r.bottom()).has_value());  // bottom edge is padding
  }
  CHECK_FALSE(tl.hit_test(tl.pill.x + 2.0F, tl.pill.cy()).has_value());     // pill padding
  CHECK_FALSE(tl.hit_test(kTabsBar.x + 10.0F, kTabsBar.cy()).has_value());  // bar outside the pill
  CHECK_FALSE(tl.hit_test(tl.pill.cx(), kTabsBar.y - 1.0F).has_value());
}

TEST_CASE("tabs: TabsModel defaults to CarPlay with nothing pressed", "[gui_rendering][tabs]") {
  const TabsModel m{};
  CHECK(m.active == Tab::CarPlay);
  CHECK_FALSE(m.reverse);
  CHECK_FALSE(m.pressed.has_value());
}

TEST_CASE("tabs: draw_tabs fills only the active button in ember and marks reverse on the Camera tab",
          "[gui_rendering][tabs][draw]") {
  NullCanvas canvas;
  const Theme t = Theme::dark();
  const TabsLayout tl = TabsLayout::compute(kTabsBar);

  c3::render::draw_tabs(canvas.vg(), tl, t, Fonts{}, TabsModel{.active = Tab::Settings});
  CHECK(canvas.calls().fills_with(t.ember) == 1);
  CHECK(canvas.calls().fills_with(t.alarm) == 0);
  CHECK(canvas.calls().fills_with(t.surface2) == 0);
  const auto active = std::find_if(canvas.calls().fills.begin(), canvas.calls().fills.end(),
                                   [&](const FillCall& f) { return same_color(f.inner, t.ember); });
  REQUIRE(active != canvas.calls().fills.end());
  expect_rect(active->bounds, tl.rect(Tab::Settings));

  canvas.clear();
  c3::render::draw_tabs(canvas.vg(), tl, t, Fonts{},
                        TabsModel{.active = Tab::CarPlay, .reverse = true, .pressed = Tab::Camera});
  CHECK(canvas.calls().fills_with(t.ember) == 1);
  CHECK(canvas.calls().fills_with(t.alarm) == 1);     // the reverse dot
  CHECK(canvas.calls().fills_with(t.surface2) == 1);  // the pressed highlight
  CHECK(canvas.transform_is_identity());
}

}  // namespace tabs_tests

// ===========================================================================
// status_tools.hpp
// ===========================================================================
namespace status_tools_tests {

using c3::render::kToolCount;
using c3::render::Tool;
using c3::render::ToolsLayout;
using c3::render::ToolsModel;

constexpr Rect kStatusBar{.x = 40.0F, .y = 0.0F, .w = 1520.0F, .h = 88.0F};

TEST_CASE("status_tools: the theme button is a 64-unit disc flush with the right end of the bar",
          "[gui_rendering][status_tools]") {
  STATIC_CHECK(kToolCount == 1);
  const ToolsLayout tl = ToolsLayout::compute(kStatusBar);
  CHECK(tl.scale == 1.0F);
  expect_rect(tl.rect(Tool::Theme), {.x = 1496.0F, .y = 12.0F, .w = 64.0F, .h = 64.0F});
  CHECK(tl.left() == 1496.0F);
  CHECK_THAT(tl.rect(Tool::Theme).cy(), WithinAbs(kStatusBar.cy(), 1e-3));
  CHECK(tl.rect(Tool::Theme).right() == kStatusBar.right());
}

TEST_CASE("status_tools: the layout scales with the bar height", "[gui_rendering][status_tools]") {
  const ToolsLayout half = ToolsLayout::compute(scaled(kStatusBar, 0.5F));
  CHECK(half.scale == 0.5F);
  expect_rect(half.rect(Tool::Theme), {.x = 748.0F, .y = 6.0F, .w = 32.0F, .h = 32.0F});
}

TEST_CASE("status_tools: hit_test finds the button and nothing beside it", "[gui_rendering][status_tools]") {
  const ToolsLayout tl = ToolsLayout::compute(kStatusBar);
  const Rect& r = tl.rect(Tool::Theme);
  CHECK(tl.hit_test(r.cx(), r.cy()) == Tool::Theme);
  CHECK(tl.hit_test(r.x, r.y) == Tool::Theme);
  CHECK_FALSE(tl.hit_test(r.x - 1.0F, r.cy()).has_value());
  CHECK_FALSE(tl.hit_test(r.right(), r.cy()).has_value());
  CHECK_FALSE(tl.hit_test(r.cx(), r.bottom()).has_value());
}

TEST_CASE("status_tools: draw_status_tools draws one round button, pressed or not",
          "[gui_rendering][status_tools][draw]") {
  NullCanvas canvas;
  const Theme t = Theme::dark();
  const ToolsLayout tl = ToolsLayout::compute(kStatusBar);
  c3::render::draw_status_tools(canvas.vg(), tl, t, ToolsModel{.dark = true});
  CHECK(canvas.calls().fills_with(t.surface) == 1);
  CHECK(canvas.calls().fills_with(t.surface2) == 0);
  canvas.clear();
  c3::render::draw_status_tools(canvas.vg(), tl, t, ToolsModel{.dark = false, .pressed = Tool::Theme});
  CHECK(canvas.calls().fills_with(t.surface2) == 1);
  CHECK(canvas.transform_is_identity());
}

}  // namespace status_tools_tests

// ===========================================================================
// dock.hpp
// ===========================================================================
namespace dock_tests {

using c3::render::DockControl;
using c3::render::DockLayout;
using c3::render::DockModel;
using c3::render::kDockControlCount;
using c3::vehicle::CommandStatus;
using c3::vehicle::HvacBlower;
using c3::vehicle::HvacMode;
using c3::vehicle::HvacState;
using c3::vehicle::LampState;

constexpr Rect kDock{.x = 40.0F, .y = 2128.0F, .w = 1520.0F, .h = 392.0F};

TEST_CASE("dock: the reference dock splits into the Lights and HVAC islands with a 16-unit gap",
          "[gui_rendering][dock]") {
  const DockLayout dl = DockLayout::compute(kDock);
  CHECK(dl.scale == 1.0F);
  CHECK(dl.dock == kDock);
  expect_rect(dl.lights_island, {.x = 40.0F, .y = 2128.0F, .w = 496.0F, .h = 392.0F});
  expect_rect(dl.hvac_island, {.x = 552.0F, .y = 2128.0F, .w = 1008.0F, .h = 392.0F});
  CHECK_THAT(dl.hvac_island.x - dl.lights_island.right(), WithinAbs(16.0, 1e-3));
  CHECK_THAT(dl.hvac_island.right(), WithinAbs(kDock.right(), 1e-3));
}

TEST_CASE("dock: the HVAC island stacks mode, blower and A/C rows under its title", "[gui_rendering][dock]") {
  const DockLayout dl = DockLayout::compute(kDock);
  expect_rect(dl.mode_group, {.x = 576.0F, .y = 2190.0F, .w = 960.0F, .h = 96.0F});
  expect_rect(dl.blower_group, {.x = 576.0F, .y = 2296.0F, .w = 960.0F, .h = 96.0F});
  expect_rect(dl.rect(DockControl::Ac), {.x = 576.0F, .y = 2402.0F, .w = 960.0F, .h = 96.0F});

  // three equal mode buttons inset by the segment padding
  expect_rect(dl.rect(DockControl::ModeVent), {.x = 582.0F, .y = 2196.0F, .w = 312.0F, .h = 84.0F});
  expect_rect(dl.rect(DockControl::ModeFloor), {.x = 900.0F, .y = 2196.0F, .w = 312.0F, .h = 84.0F});
  expect_rect(dl.rect(DockControl::ModeDefrost), {.x = 1218.0F, .y = 2196.0F, .w = 312.0F, .h = 84.0F});

  // five equal blower buttons
  expect_rect(dl.rect(DockControl::BlowerOff), {.x = 582.0F, .y = 2302.0F, .w = 184.8F, .h = 84.0F}, 1e-2F);
  expect_rect(dl.rect(DockControl::Blower4), {.x = 1345.2F, .y = 2302.0F, .w = 184.8F, .h = 84.0F}, 1e-2F);
  CHECK_THAT(dl.rect(DockControl::Blower4).right(), WithinAbs(dl.blower_group.right() - 6.0, 1e-2));
}

TEST_CASE("dock: the Lights island holds three equal toggles, doors first, above the hint", "[gui_rendering][dock]") {
  const DockLayout dl = DockLayout::compute(kDock);
  expect_rect(dl.rect(DockControl::Doors), {.x = 64.0F, .y = 2190.0F, .w = 448.0F, .h = 85.3333F}, 1e-2F);
  expect_rect(dl.rect(DockControl::Headlamps), {.x = 64.0F, .y = 2285.3333F, .w = 448.0F, .h = 85.3333F}, 1e-2F);
  expect_rect(dl.rect(DockControl::HighBeams), {.x = 64.0F, .y = 2380.6667F, .w = 448.0F, .h = 85.3333F}, 1e-2F);
  // the hint strip (20 bottom pad + 26 hint + 8 gap) stays clear below the last toggle
  CHECK_THAT(kDock.bottom() - dl.rect(DockControl::HighBeams).bottom(), WithinAbs(54.0, 1e-2));
}

TEST_CASE("dock: every control lies inside its island and controls never overlap", "[gui_rendering][dock]") {
  const DockLayout dl = DockLayout::compute(kDock);
  constexpr std::array<DockControl, 3> kLights = {DockControl::Doors, DockControl::Headlamps, DockControl::HighBeams};
  for (std::size_t i = 0; i < kDockControlCount; ++i) {
    const auto c = static_cast<DockControl>(i);
    INFO(Catch::StringMaker<DockControl>::convert(c));
    const bool lights = std::find(kLights.begin(), kLights.end(), c) != kLights.end();
    CHECK(inside(dl.controls[i], lights ? dl.lights_island : dl.hvac_island));
    CHECK(dl.controls[i].w > 0.0F);
    for (std::size_t j = i + 1; j < kDockControlCount; ++j) {
      CHECK_FALSE(overlaps(dl.controls[i], dl.controls[j]));
    }
  }
  for (std::size_t i = 0; i < 3; ++i) {
    CHECK(inside(dl.controls[static_cast<std::size_t>(DockControl::ModeVent) + i], dl.mode_group));
  }
  for (std::size_t i = 0; i < 5; ++i) {
    CHECK(inside(dl.controls[static_cast<std::size_t>(DockControl::BlowerOff) + i], dl.blower_group));
  }
}

TEST_CASE("dock: the layout scales uniformly with the dock width", "[gui_rendering][dock]") {
  const DockLayout ref = DockLayout::compute(kDock);
  const DockLayout half = DockLayout::compute(scaled(kDock, 0.5F));
  CHECK(half.scale == 0.5F);
  expect_rect(half.lights_island, scaled(ref.lights_island, 0.5F), 1e-2F);
  expect_rect(half.hvac_island, scaled(ref.hvac_island, 0.5F), 1e-2F);
  for (std::size_t i = 0; i < kDockControlCount; ++i) {
    expect_rect(half.controls[i], scaled(ref.controls[i], 0.5F), 1e-2F);
  }
}

TEST_CASE("dock: hit_test maps each control's centre to itself and chrome to nothing", "[gui_rendering][dock]") {
  const DockLayout dl = DockLayout::compute(kDock);
  for (std::size_t i = 0; i < kDockControlCount; ++i) {
    const auto c = static_cast<DockControl>(i);
    CHECK(dl.hit_test(dl.rect(c).cx(), dl.rect(c).cy()) == c);
  }
  // the gap between the islands, the HVAC title, the segment padding, the lights hint
  CHECK_FALSE(dl.hit_test(dl.lights_island.right() + 8.0F, kDock.cy()).has_value());
  CHECK_FALSE(dl.hit_test(dl.hvac_island.x + 40.0F, kDock.y + 30.0F).has_value());
  CHECK_FALSE(dl.hit_test(dl.mode_group.x + 2.0F, dl.mode_group.cy()).has_value());
  CHECK_FALSE(dl.hit_test(dl.lights_island.cx(), kDock.bottom() - 20.0F).has_value());
  CHECK_FALSE(dl.hit_test(kDock.x - 1.0F, kDock.cy()).has_value());
}

TEST_CASE("dock: draw_dock selects nothing without a live frame and the relay state with one",
          "[gui_rendering][dock][draw]") {
  NullCanvas canvas;
  const Theme t = Theme::dark();
  const DockLayout dl = DockLayout::compute(kDock);

  DockModel m{.hvac = HvacState{.mode = HvacMode::Floor, .blower = HvacBlower::L2, .ac_enabled = true},
              .lamps = LampState{.lamp_doors_open = true, .headlights_on = true},
              .live = false,
              .doors_known = true};
  c3::render::draw_dock(canvas.vg(), dl, t, Fonts{}, m);
  CHECK(canvas.calls().fills_with(t.ember) == 0);  // UNKNOWN everywhere: nothing is "on"

  canvas.clear();
  m.live = true;
  c3::render::draw_dock(canvas.vg(), dl, t, Fonts{}, m);
  // Floor, blower 2, A/C, doors, lights are on; high beams are unknown and off
  CHECK(canvas.calls().fills_with(t.ember) == 5);

  canvas.clear();
  m.pressed = DockControl::Blower4;
  c3::render::draw_dock(canvas.vg(), dl, t, Fonts{}, m);
  CHECK(canvas.calls().fills_with(t.surface2) == 1);
  CHECK(canvas.transform_is_identity());
}

TEST_CASE("dock: DockModel defaults to an unknown, idle dock", "[gui_rendering][dock]") {
  const DockModel m{};
  CHECK_FALSE(m.live);
  CHECK_FALSE(m.doors_known);
  CHECK_FALSE(m.high_known);
  CHECK_FALSE(m.pressed.has_value());
  CHECK(m.command == CommandStatus::Idle);
}

}  // namespace dock_tests

// ===========================================================================
// camera_surface.hpp
// ===========================================================================
namespace camera_tests {

using c3::render::CameraControl;
using c3::render::CameraLayout;
using c3::render::CameraModel;
using c3::render::CameraState;

TEST_CASE("camera: the tools sit in the well's top-right corner, guides then close", "[gui_rendering][camera]") {
  const CameraLayout cl = CameraLayout::compute(kReferenceWell);
  CHECK(cl.scale == 1.0F);
  CHECK(cl.well == kReferenceWell);
  expect_rect(cl.close_button, {.x = 1464.0F, .y = 124.0F, .w = 72.0F, .h = 72.0F});
  expect_rect(cl.guides_button, {.x = 1380.0F, .y = 124.0F, .w = 72.0F, .h = 72.0F});
  CHECK_THAT(kReferenceWell.right() - cl.close_button.right(), WithinAbs(24.0, 1e-3));
  CHECK_THAT(cl.close_button.x - cl.guides_button.right(), WithinAbs(12.0, 1e-3));
  CHECK(inside(cl.guides_button, kReferenceWell));
  CHECK(inside(cl.close_button, kReferenceWell));
}

TEST_CASE("camera: the layout scales with the well width", "[gui_rendering][camera]") {
  const CameraLayout half = CameraLayout::compute(scaled(kReferenceWell, 0.5F));
  CHECK(half.scale == 0.5F);
  expect_rect(half.close_button, {.x = 732.0F, .y = 62.0F, .w = 36.0F, .h = 36.0F});
  expect_rect(half.guides_button, {.x = 690.0F, .y = 62.0F, .w = 36.0F, .h = 36.0F});
}

TEST_CASE("camera: close exists only in Preview; guides always", "[gui_rendering][camera]") {
  const CameraLayout cl = CameraLayout::compute(kReferenceWell);
  const Rect& g = cl.guides_button;
  const Rect& c = cl.close_button;
  for (const CameraState s : {CameraState::Preview, CameraState::Reverse, CameraState::Hold}) {
    CHECK(cl.hit_test(g.cx(), g.cy(), s) == CameraControl::Guides);
    CHECK_FALSE(cl.hit_test(g.x - 1.0F, g.cy(), s).has_value());
    CHECK_FALSE(cl.hit_test(kReferenceWell.cx(), kReferenceWell.cy(), s).has_value());
  }
  CHECK(cl.hit_test(c.cx(), c.cy(), CameraState::Preview) == CameraControl::Close);
  CHECK_FALSE(cl.hit_test(c.cx(), c.cy(), CameraState::Reverse).has_value());
  CHECK_FALSE(cl.hit_test(c.cx(), c.cy(), CameraState::Hold).has_value());
}

TEST_CASE("camera: draw_camera clips everything to the well and omits the close button while reversing",
          "[gui_rendering][camera][draw]") {
  NullCanvas canvas;
  const Theme t = Theme::dark();
  const CameraLayout cl = CameraLayout::compute(kReferenceWell);

  c3::render::draw_camera(canvas.vg(), cl, t, Fonts{}, CameraModel{.state = CameraState::Preview});
  REQUIRE_FALSE(canvas.calls().fills.empty());
  for (const FillCall& f : canvas.calls().fills) {
    REQUIRE(f.scissor.has_value());
    expect_rect(*f.scissor, kReferenceWell);
  }
  // no frames: the picture area is the "camera unavailable" panel, never black video
  CHECK(canvas.calls().fills_with(rgb(0x05060A)) == 1);
  const std::size_t preview_fills = canvas.calls().fills.size();

  canvas.clear();
  c3::render::draw_camera(canvas.vg(), cl, t, Fonts{}, CameraModel{.state = CameraState::Reverse});
  CHECK(canvas.calls().fills.size() == preview_fills - 1);  // the close button's disc
  CHECK(canvas.calls().fills_with(t.alarm) == 1);           // the REVERSE badge

  canvas.clear();
  c3::render::draw_camera(canvas.vg(), cl, t, Fonts{}, CameraModel{.state = CameraState::Hold});
  CHECK(canvas.calls().fills.size() == preview_fills - 1);
  CHECK(canvas.transform_is_identity());
}

TEST_CASE("camera: CameraModel defaults to a preview with no frames and guides on", "[gui_rendering][camera]") {
  const CameraModel m{};
  CHECK(m.state == CameraState::Preview);
  CHECK_FALSE(m.frames);
  CHECK(m.guides);
  CHECK_FALSE(m.pressed.has_value());
}

}  // namespace camera_tests

// ===========================================================================
// sheet.hpp
// ===========================================================================
namespace sheet_tests {

using c3::render::compute_pages;
using c3::render::is_slider;
using c3::render::kPageCount;
using c3::render::kSheetControlCount;
using c3::render::Page;
using c3::render::page_for;
using c3::render::PageLayouts;
using c3::render::SheetControl;
using c3::render::SheetLayout;
using c3::render::SheetModel;

constexpr std::array<Page, 6> kRealPages = {Page::Root,  Page::Audio,       Page::Theme,
                                            Page::Units, Page::Calibration, Page::System};

[[nodiscard]] constexpr SheetControl control_at(std::size_t i) noexcept {
  return static_cast<SheetControl>(i);
}

[[nodiscard]] std::vector<SheetControl> present_controls(const SheetLayout& sl) {
  std::vector<SheetControl> out;
  for (std::size_t i = 0; i < kSheetControlCount; ++i) {
    if (sl.has(control_at(i))) {
      out.push_back(control_at(i));
    }
  }
  return out;
}

[[nodiscard]] SheetLayout reference(Page page) noexcept {
  return SheetLayout::compute(page, kReferenceWell, kReferencePanel);
}

TEST_CASE("sheet: is_slider marks the eight continuous controls", "[gui_rendering][sheet]") {
  STATIC_CHECK(is_slider(SheetControl::Volume));
  STATIC_CHECK(is_slider(SheetControl::Balance));
  STATIC_CHECK(is_slider(SheetControl::Fader));
  STATIC_CHECK(is_slider(SheetControl::Sub));
  STATIC_CHECK(is_slider(SheetControl::Bass));
  STATIC_CHECK(is_slider(SheetControl::Mid));
  STATIC_CHECK(is_slider(SheetControl::Treble));
  STATIC_CHECK(is_slider(SheetControl::Hue));
  std::size_t sliders = 0;
  for (std::size_t i = 0; i < kSheetControlCount; ++i) {
    sliders += is_slider(control_at(i)) ? 1 : 0;
  }
  CHECK(sliders == 8);
  STATIC_CHECK_FALSE(is_slider(SheetControl::Mute));
  STATIC_CHECK_FALSE(is_slider(SheetControl::Swatch0));
  STATIC_CHECK_FALSE(is_slider(SheetControl::Close));
}

TEST_CASE("sheet: page_for maps the root rows to their pages and nothing else", "[gui_rendering][sheet]") {
  STATIC_CHECK(page_for(SheetControl::RowAudio) == Page::Audio);
  STATIC_CHECK(page_for(SheetControl::RowTheme) == Page::Theme);
  STATIC_CHECK(page_for(SheetControl::RowUnits) == Page::Units);
  STATIC_CHECK(page_for(SheetControl::RowCalibration) == Page::Calibration);
  STATIC_CHECK(page_for(SheetControl::RowSystem) == Page::System);
  STATIC_CHECK_FALSE(page_for(SheetControl::Close).has_value());
  STATIC_CHECK_FALSE(page_for(SheetControl::Back).has_value());
  STATIC_CHECK_FALSE(page_for(SheetControl::Volume).has_value());
  STATIC_CHECK_FALSE(page_for(SheetControl::Reset).has_value());
}

TEST_CASE("sheet: None and Count have no frame and no controls", "[gui_rendering][sheet]") {
  for (const Page p : {Page::None, Page::Count}) {
    const SheetLayout sl = reference(p);
    CHECK(sl.kind == p);
    CHECK(sl.frame == Rect{});
    CHECK(present_controls(sl).empty());
    CHECK(sl.card_count == 0);
    CHECK_FALSE(sl.hit_test(kReferenceWell.cx(), kReferenceWell.cy()).has_value());
  }
}

TEST_CASE("sheet: every page frames the whole well, closes top-right and backs top-left below the root",
          "[gui_rendering][sheet]") {
  for (const Page p : kRealPages) {
    INFO(Catch::StringMaker<Page>::convert(p));
    const SheetLayout sl = reference(p);
    CHECK(sl.scale == 1.0F);
    CHECK(sl.frame == kReferenceWell);
    CHECK(sl.panel == kReferencePanel);
    REQUIRE(sl.has(SheetControl::Close));
    expect_rect(sl.rect(SheetControl::Close), {.x = 1448.0F, .y = 132.0F, .w = 72.0F, .h = 72.0F});
    if (p == Page::Root) {
      CHECK_FALSE(sl.has(SheetControl::Back));
    } else {
      REQUIRE(sl.has(SheetControl::Back));
      expect_rect(sl.rect(SheetControl::Back), {.x = 80.0F, .y = 132.0F, .w = 72.0F, .h = 72.0F});
    }
  }
}

TEST_CASE("sheet: the root page is five full-width rows and nothing else", "[gui_rendering][sheet]") {
  const SheetLayout sl = reference(Page::Root);
  constexpr std::array<SheetControl, 5> kRows = {SheetControl::RowAudio, SheetControl::RowTheme, SheetControl::RowUnits,
                                                 SheetControl::RowCalibration, SheetControl::RowSystem};
  for (std::size_t i = 0; i < kRows.size(); ++i) {
    expect_rect(sl.rect(kRows[i]),
                {.x = 80.0F, .y = 232.0F + 192.0F * static_cast<float>(i), .w = 1440.0F, .h = 176.0F});
  }
  CHECK(present_controls(sl).size() == 6);  // close + five rows
  CHECK(sl.card_count == 0);
}

TEST_CASE("sheet: the audio page places volume and mute on the first card and the tone sliders below and right",
          "[gui_rendering][sheet]") {
  const SheetLayout sl = reference(Page::Audio);
  REQUIRE(sl.card_count == 3);
  expect_rect(sl.cards[0], {.x = 80.0F, .y = 232.0F, .w = 708.0F, .h = 180.0F});
  expect_rect(sl.cards[1], {.x = 80.0F, .y = 436.0F, .w = 708.0F, .h = 1016.0F});
  expect_rect(sl.cards[2], {.x = 812.0F, .y = 232.0F, .w = 708.0F, .h = 1220.0F});

  expect_rect(sl.rect(SheetControl::Volume), {.x = 112.0F, .y = 306.0F, .w = 328.0F, .h = 72.0F});
  expect_rect(sl.rect(SheetControl::Mute), {.x = 456.0F, .y = 260.0F, .w = 300.0F, .h = 128.0F});
  expect_rect(sl.rect(SheetControl::Balance), {.x = 112.0F, .y = 500.0F, .w = 644.0F, .h = 72.0F});
  expect_rect(sl.rect(SheetControl::Fader), {.x = 112.0F, .y = 646.0F, .w = 644.0F, .h = 72.0F});
  expect_rect(sl.rect(SheetControl::Sub), {.x = 112.0F, .y = 792.0F, .w = 644.0F, .h = 72.0F});
  expect_rect(sl.rect(SheetControl::Bass), {.x = 844.0F, .y = 296.0F, .w = 644.0F, .h = 72.0F});
  expect_rect(sl.rect(SheetControl::Mid), {.x = 844.0F, .y = 422.0F, .w = 644.0F, .h = 72.0F});
  expect_rect(sl.rect(SheetControl::Treble), {.x = 844.0F, .y = 548.0F, .w = 644.0F, .h = 72.0F});
  expect_rect(sl.preset_group, {.x = 844.0F, .y = 674.0F, .w = 644.0F, .h = 128.0F});

  for (const SheetControl c : {SheetControl::Volume, SheetControl::Mute}) {
    CHECK(inside(sl.rect(c), sl.cards[0]));
  }
  for (const SheetControl c : {SheetControl::Balance, SheetControl::Fader, SheetControl::Sub}) {
    CHECK(inside(sl.rect(c), sl.cards[1]));
  }
  for (const SheetControl c : {SheetControl::Bass, SheetControl::Mid, SheetControl::Treble, SheetControl::PresetFlat,
                               SheetControl::PresetRock, SheetControl::PresetCruise, SheetControl::PresetVoice}) {
    CHECK(inside(sl.rect(c), sl.cards[2]));
  }
  for (std::size_t i = 0; i < 4; ++i) {
    CHECK(inside(sl.controls[static_cast<std::size_t>(SheetControl::PresetFlat) + i], sl.preset_group));
  }
  CHECK_FALSE(sl.has(SheetControl::ThemeDark));
  CHECK_FALSE(sl.has(SheetControl::TempF));
  CHECK_FALSE(sl.has(SheetControl::Reset));
  CHECK_FALSE(sl.has(SheetControl::RowAudio));
}

TEST_CASE("sheet: the theme page lays nine swatches in rows of six above the hue slider", "[gui_rendering][sheet]") {
  const SheetLayout sl = reference(Page::Theme);
  REQUIRE(sl.card_count == 3);
  expect_rect(sl.theme_group, {.x = 112.0F, .y = 296.0F, .w = 644.0F, .h = 128.0F});
  expect_rect(sl.preview, {.x = 112.0F, .y = 657.0F, .w = 644.0F, .h = 96.0F});
  expect_rect(sl.rect(SheetControl::Swatch0), {.x = 848.0F, .y = 312.0F, .w = 84.0F, .h = 84.0F});
  expect_rect(sl.rect(SheetControl::Swatch5), {.x = 1368.0F, .y = 312.0F, .w = 84.0F, .h = 84.0F});
  expect_rect(sl.rect(SheetControl::Swatch6), {.x = 848.0F, .y = 416.0F, .w = 84.0F, .h = 84.0F});
  expect_rect(sl.rect(SheetControl::Swatch8), {.x = 1056.0F, .y = 416.0F, .w = 84.0F, .h = 84.0F});
  expect_rect(sl.rect(SheetControl::Hue), {.x = 844.0F, .y = 560.0F, .w = 644.0F, .h = 72.0F});

  for (std::size_t i = 0; i < 9; ++i) {
    const Rect& sw = sl.controls[static_cast<std::size_t>(SheetControl::Swatch0) + i];
    CHECK(inside(sw, sl.cards[2]));
    CHECK(sw.y == (i < 6 ? 312.0F : 416.0F));
    CHECK(sw.bottom() <= sl.rect(SheetControl::Hue).y - 36.0F);  // clear of the hue label row
  }
  for (std::size_t i = 0; i < 3; ++i) {
    CHECK(inside(sl.controls[static_cast<std::size_t>(SheetControl::ThemeDark) + i], sl.theme_group));
  }
  CHECK(inside(sl.theme_group, sl.cards[0]));
  CHECK(inside(sl.preview, sl.cards[1]));
  CHECK_FALSE(sl.has(SheetControl::Volume));
}

TEST_CASE("sheet: the units page is two cards with one segmented pair each", "[gui_rendering][sheet]") {
  const SheetLayout sl = reference(Page::Units);
  REQUIRE(sl.card_count == 2);
  expect_rect(sl.temp_group, {.x = 112.0F, .y = 296.0F, .w = 644.0F, .h = 128.0F});
  expect_rect(sl.press_group, {.x = 844.0F, .y = 296.0F, .w = 644.0F, .h = 128.0F});
  expect_rect(sl.rect(SheetControl::TempF), {.x = 118.0F, .y = 302.0F, .w = 313.0F, .h = 116.0F});
  expect_rect(sl.rect(SheetControl::TempC), {.x = 437.0F, .y = 302.0F, .w = 313.0F, .h = 116.0F});
  CHECK(inside(sl.rect(SheetControl::PressPsi), sl.press_group));
  CHECK(inside(sl.rect(SheetControl::PressBar), sl.press_group));
  CHECK(present_controls(sl).size() == 6);  // close, back, two pairs
}

TEST_CASE("sheet: the calibration page is a banner over a 2x2 grid of channel cards in channel order",
          "[gui_rendering][sheet]") {
  const SheetLayout sl = reference(Page::Calibration);
  expect_rect(sl.banner, {.x = 80.0F, .y = 232.0F, .w = 1440.0F, .h = 116.0F});
  REQUIRE(sl.card_count == c3::telemetry::kChannelCount);
  expect_rect(sl.cards[static_cast<std::size_t>(Channel::Fuel)], {.x = 80.0F, .y = 372.0F, .w = 708.0F, .h = 528.0F});
  expect_rect(sl.cards[static_cast<std::size_t>(Channel::Oil)], {.x = 812.0F, .y = 372.0F, .w = 708.0F, .h = 528.0F});
  expect_rect(sl.cards[static_cast<std::size_t>(Channel::Coolant)],
              {.x = 80.0F, .y = 924.0F, .w = 708.0F, .h = 528.0F});
  expect_rect(sl.cards[static_cast<std::size_t>(Channel::Volt)], {.x = 812.0F, .y = 924.0F, .w = 708.0F, .h = 528.0F});
  CHECK(present_controls(sl).size() == 2);  // close and back only: nothing to touch yet
}

TEST_CASE("sheet: the system page keeps reset at the foot of the sensor card above the powered-by row",
          "[gui_rendering][sheet]") {
  const SheetLayout sl = reference(Page::System);
  REQUIRE(sl.card_count == 2);
  expect_rect(sl.cards[0], {.x = 80.0F, .y = 232.0F, .w = 708.0F, .h = 1152.0F});
  expect_rect(sl.cards[1], {.x = 812.0F, .y = 232.0F, .w = 708.0F, .h = 1152.0F});
  expect_rect(sl.kv, {.x = 112.0F, .y = 306.0F, .w = 644.0F, .h = 252.0F});
  expect_rect(sl.kv2, {.x = 844.0F, .y = 306.0F, .w = 644.0F, .h = 189.0F});
  expect_rect(sl.rect(SheetControl::Reset), {.x = 844.0F, .y = 1232.0F, .w = 644.0F, .h = 128.0F});
  expect_rect(sl.powered, {.x = 80.0F, .y = 1408.0F, .w = 1440.0F, .h = 44.0F});
  CHECK(inside(sl.kv, sl.cards[0]));
  CHECK(inside(sl.rect(SheetControl::Reset), sl.cards[1]));
  CHECK(sl.powered.y >= sl.cards[0].bottom());
  CHECK_THAT(sl.powered.bottom(), WithinAbs(kReferenceWell.bottom() - 40.0, 1e-3));
}

TEST_CASE("sheet: on every page the controls and cards stay inside the frame and never overlap",
          "[gui_rendering][sheet]") {
  for (const Page p : kRealPages) {
    INFO(Catch::StringMaker<Page>::convert(p));
    const SheetLayout sl = reference(p);
    const std::vector<SheetControl> present = present_controls(sl);
    for (std::size_t i = 0; i < present.size(); ++i) {
      INFO(Catch::StringMaker<SheetControl>::convert(present[i]));
      CHECK(inside(sl.rect(present[i]), sl.frame));
      for (std::size_t j = i + 1; j < present.size(); ++j) {
        CHECK_FALSE(overlaps(sl.rect(present[i]), sl.rect(present[j])));
      }
    }
    for (std::size_t i = 0; i < sl.card_count; ++i) {
      CHECK(inside(sl.cards[i], sl.frame));
      for (std::size_t j = i + 1; j < sl.card_count; ++j) {
        CHECK_FALSE(overlaps(sl.cards[i], sl.cards[j]));
      }
    }
  }
}

TEST_CASE("sheet: hit_test returns each present control at its centre and nothing between them",
          "[gui_rendering][sheet]") {
  for (const Page p : kRealPages) {
    INFO(Catch::StringMaker<Page>::convert(p));
    const SheetLayout sl = reference(p);
    for (const SheetControl c : present_controls(sl)) {
      INFO(Catch::StringMaker<SheetControl>::convert(c));
      CHECK(sl.hit_test(sl.rect(c).cx(), sl.rect(c).cy()) == c);
    }
    // the title area between back and close is never a control
    CHECK_FALSE(sl.hit_test(kReferenceWell.cx(), 168.0F).has_value());
    CHECK_FALSE(sl.hit_test(kReferenceWell.x - 1.0F, 168.0F).has_value());
  }
}

TEST_CASE("sheet: a slider's label row is part of its touch target; the row above it is not",
          "[gui_rendering][sheet]") {
  const SheetLayout sl = reference(Page::Audio);
  const Rect& balance = sl.rect(SheetControl::Balance);
  CHECK(sl.hit_test(balance.cx(), balance.y - 1.0F) == SheetControl::Balance);
  CHECK(sl.hit_test(balance.cx(), balance.y - 36.0F) == SheetControl::Balance);
  CHECK_FALSE(sl.hit_test(balance.cx(), balance.y - 37.0F).has_value());
  CHECK_FALSE(sl.hit_test(balance.cx(), balance.bottom()).has_value());  // the end captions below

  // a button has no such extension
  const Rect& mute = sl.rect(SheetControl::Mute);
  CHECK_FALSE(sl.hit_test(mute.cx(), mute.y - 1.0F).has_value());
}

TEST_CASE("sheet: the layout scales uniformly with the well", "[gui_rendering][sheet]") {
  for (const Page p : kRealPages) {
    INFO(Catch::StringMaker<Page>::convert(p));
    const SheetLayout ref = reference(p);
    const SheetLayout half = SheetLayout::compute(p, scaled(kReferenceWell, 0.5F), kPanel);
    CHECK(half.scale == 0.5F);
    CHECK(half.card_count == ref.card_count);
    for (std::size_t i = 0; i < kSheetControlCount; ++i) {
      expect_rect(half.controls[i], scaled(ref.controls[i], 0.5F), 1e-2F);
    }
    for (std::size_t i = 0; i < ref.card_count; ++i) {
      expect_rect(half.cards[i], scaled(ref.cards[i], 0.5F), 1e-2F);
    }
  }
}

TEST_CASE("sheet: compute_pages indexes one layout per Page", "[gui_rendering][sheet]") {
  const PageLayouts pages = compute_pages(Layout::compute(kReferencePanel));
  STATIC_CHECK(kPageCount == 7);  // None plus the root and five pages
  for (std::size_t i = 0; i < kPageCount; ++i) {
    CHECK(pages[i].kind == static_cast<Page>(i));
    CHECK(pages[i].well == kReferenceWell);
  }
  CHECK(present_controls(pages[static_cast<std::size_t>(Page::None)]).empty());
  CHECK(present_controls(pages[static_cast<std::size_t>(Page::Root)]).size() == 6);
}

TEST_CASE("sheet: draw_sheet draws nothing for None and a themed panel for a page", "[gui_rendering][sheet][draw]") {
  NullCanvas canvas;
  const Theme t = Theme::dark();
  const SheetModel m{};

  c3::render::draw_sheet(canvas.vg(), reference(Page::None), t, Fonts{}, m);
  CHECK(canvas.calls().fills.empty());
  CHECK(canvas.calls().strokes.empty());

  for (const Page p : kRealPages) {
    INFO(Catch::StringMaker<Page>::convert(p));
    canvas.clear();
    c3::render::draw_sheet(canvas.vg(), reference(p), t, Fonts{}, m);
    REQUIRE_FALSE(canvas.calls().fills.empty());
    expect_rect(canvas.calls().fills.front().bounds, kReferenceWell);  // the sheet panel first
    CHECK(same_color(canvas.calls().fills.front().inner, t.sheet));
    CHECK(canvas.transform_is_identity());
  }
}

TEST_CASE("sheet: a pressed root row is highlighted in surface2", "[gui_rendering][sheet][draw]") {
  NullCanvas canvas;
  const Theme t = Theme::dark();
  c3::render::draw_sheet(canvas.vg(), reference(Page::Root), t, Fonts{}, SheetModel{});
  const std::size_t idle = canvas.calls().fills_with(t.surface2);
  canvas.clear();
  c3::render::draw_sheet(canvas.vg(), reference(Page::Root), t, Fonts{}, SheetModel{.pressed = SheetControl::RowAudio});
  CHECK(canvas.calls().fills_with(t.surface2) == idle + 1);
}

TEST_CASE("sheet: the calibration banner is amber for a placeholder table and neutral for a measured one",
          "[gui_rendering][sheet][draw]") {
  NullCanvas canvas;
  const Theme t = Theme::dark();
  const SheetLayout sl = reference(Page::Calibration);

  // null table reads as placeholder (safety invariant 5: say so before showing a number)
  c3::render::draw_sheet(canvas.vg(), sl, t, Fonts{}, SheetModel{});
  CHECK(canvas.calls().fills_with(c3::render::draw::alpha(t.amber, 0.14F)) == 1);

  c3::telemetry::CalibrationTable measured;
  measured.placeholder = false;
  canvas.clear();
  c3::render::draw_sheet(canvas.vg(), sl, t, Fonts{}, SheetModel{.calibration = &measured});
  CHECK(canvas.calls().fills_with(c3::render::draw::alpha(t.amber, 0.14F)) == 0);

  // a live frame with a table draws without needing fonts
  const Frame frame = make_frame();
  canvas.clear();
  c3::render::draw_sheet(canvas.vg(), sl, t, Fonts{},
                         SheetModel{.calibration = &measured, .frame = &frame, .live = true});
  CHECK(canvas.transform_is_identity());
}

}  // namespace sheet_tests

// ===========================================================================
// alerts.hpp
// ===========================================================================
namespace alerts_tests {

using c3::render::Alert;
using c3::render::AlertLevel;
using c3::render::AlertList;
using c3::render::collect_alerts;
using c3::render::kMaxAlerts;
using c3::render::PressureUnit;
using c3::render::TempUnit;
using c3::render::Units;

TEST_CASE("alerts: with no frame at all the only banner says we are waiting for the MCU", "[gui_rendering][alerts]") {
  const AlertList alerts = collect_alerts(nullptr, false, 0);
  REQUIRE(alerts.size() == 1);
  CHECK(alerts[0].level == AlertLevel::Neutral);
  CHECK(alerts[0].title == "No telemetry");
  CHECK(alerts[0].chip == "LINK LOST");
  CHECK(alerts[0].detail_view() == "waiting for MCU");
}

TEST_CASE("alerts: a stalled link reports the frame age and that HVAC holds its last state",
          "[gui_rendering][alerts]") {
  const Frame frame = make_frame({fault(FaultKind::OilLow)});
  const AlertList alerts = collect_alerts(&frame, false, 2500);
  REQUIRE(alerts.size() == 1);  // the stale frame's faults are not shown as live
  CHECK(alerts[0].level == AlertLevel::Neutral);
  CHECK(alerts[0].chip == "LINK LOST");
  CHECK(alerts[0].detail_view() == "2.5 s since last frame \xC2\xB7 HVAC holds last state");
}

TEST_CASE("alerts: a live frame with no faults produces no banners, whatever the values or age",
          "[gui_rendering][alerts]") {
  Frame frame = make_frame();
  frame.oil_psi = 0.0F;      // nothing here decides a fault
  frame.coolant_f = 400.0F;  // the MCU does
  CHECK(collect_alerts(&frame, true, 99999).empty());
}

TEST_CASE("alerts: null frame with live set is treated as not live", "[gui_rendering][alerts]") {
  const AlertList alerts = collect_alerts(nullptr, true, 0);
  REQUIRE(alerts.size() == 1);
  CHECK(alerts[0].chip == "LINK LOST");
}

TEST_CASE("alerts: oil and coolant faults are alarms with the value in the display unit", "[gui_rendering][alerts]") {
  Frame frame = make_frame({fault(FaultKind::OilLow), fault(FaultKind::CltHigh)});
  frame.oil_psi = 8.4F;
  frame.coolant_f = 241.0F;

  const AlertList psi_f = collect_alerts(&frame, true, 0);
  REQUIRE(psi_f.size() == 2);
  CHECK(psi_f[0].level == AlertLevel::Alarm);
  CHECK(psi_f[0].title == "Low oil pressure");
  CHECK(psi_f[0].chip == "OIL");
  CHECK(psi_f[0].detail_view() == "8 psi");
  CHECK(psi_f[1].level == AlertLevel::Alarm);
  CHECK(psi_f[1].title == "Coolant overheat");
  CHECK(psi_f[1].chip == "COOLANT");
  CHECK(psi_f[1].detail_view() ==
        "241 \xC2\xB0"
        "F");

  const AlertList bar_c =
      collect_alerts(&frame, true, 0, Units{.temp = TempUnit::Celsius, .pressure = PressureUnit::Bar});
  REQUIRE(bar_c.size() == 2);
  CHECK(bar_c[0].detail_view() == "0.6 bar");
  CHECK(bar_c[1].detail_view() ==
        "116 \xC2\xB0"
        "C");
}

TEST_CASE("alerts: fuel and charging faults are cautions", "[gui_rendering][alerts]") {
  Frame frame = make_frame({fault(FaultKind::FuelLow), fault(FaultKind::VoltLow)});
  frame.fuel_fraction = 0.0625F;
  frame.voltage_v = 11.8F;
  const AlertList alerts = collect_alerts(&frame, true, 0);
  REQUIRE(alerts.size() == 2);
  CHECK(alerts[0].level == AlertLevel::Caution);
  CHECK(alerts[0].title == "Low fuel");
  CHECK(alerts[0].chip == "FUEL");
  CHECK(alerts[0].detail_view() == "6% remaining");
  CHECK(alerts[1].level == AlertLevel::Caution);
  CHECK(alerts[1].title == "Charging system");
  CHECK(alerts[1].chip == "CHARGING");
  CHECK(alerts[1].detail_view() == "11.8 V \xC2\xB7 low");

  frame.faults.clear();
  REQUIRE(frame.faults.push_back(fault(FaultKind::VoltHigh)));
  frame.voltage_v = 15.3F;
  const AlertList high = collect_alerts(&frame, true, 0);
  REQUIRE(high.size() == 1);
  CHECK(high[0].detail_view() == "15.3 V \xC2\xB7 high");
}

TEST_CASE("alerts: sender faults are alarms on the engine channels and neutral elsewhere", "[gui_rendering][alerts]") {
  const Frame frame =
      make_frame({fault(FaultKind::SenderOpen, Channel::Oil), fault(FaultKind::SenderShort, Channel::Coolant),
                  fault(FaultKind::SenderOpen, Channel::Fuel), fault(FaultKind::SenderShort, Channel::Volt)});
  const AlertList alerts = collect_alerts(&frame, true, 0);
  REQUIRE(alerts.size() == 4);
  CHECK(alerts[0].level == AlertLevel::Alarm);
  CHECK(alerts[0].title == "Oil pressure sender open");
  CHECK(alerts[1].level == AlertLevel::Alarm);
  CHECK(alerts[1].title == "Coolant sender short");
  CHECK(alerts[2].level == AlertLevel::Neutral);
  CHECK(alerts[2].title == "Fuel sender open");
  CHECK(alerts[3].level == AlertLevel::Neutral);
  CHECK(alerts[3].title == "Voltage sense short");
  for (std::size_t i = 0; i < alerts.size(); ++i) {
    CHECK(alerts[i].chip == "SENDER");
    CHECK(alerts[i].detail_view() == "check wiring");
  }
}

TEST_CASE("alerts: banners are ordered alarm, caution, neutral and are stable within a level",
          "[gui_rendering][alerts]") {
  const Frame frame = make_frame({fault(FaultKind::SenderOpen, Channel::Fuel), fault(FaultKind::VoltLow),
                                  fault(FaultKind::FuelLow), fault(FaultKind::CltHigh), fault(FaultKind::OilLow)});
  const AlertList alerts = collect_alerts(&frame, true, 0);
  REQUIRE(alerts.size() == 5);
  CHECK(alerts[0].chip == "COOLANT");  // alarms in frame order
  CHECK(alerts[1].chip == "OIL");
  CHECK(alerts[2].chip == "CHARGING");  // cautions in frame order
  CHECK(alerts[3].chip == "FUEL");
  CHECK(alerts[4].chip == "SENDER");
  CHECK(alerts[4].level == AlertLevel::Neutral);
}

TEST_CASE("alerts: a frame carrying the maximum fault count yields one banner each", "[gui_rendering][alerts]") {
  Frame frame = make_frame();
  for (std::size_t i = 0; i < c3::telemetry::kMaxFaults; ++i) {
    REQUIRE(frame.faults.push_back(fault(FaultKind::SenderOpen, static_cast<Channel>(i % 4))));
  }
  const AlertList alerts = collect_alerts(&frame, true, 0);
  CHECK(alerts.size() == c3::telemetry::kMaxFaults);
  STATIC_CHECK(kMaxAlerts == c3::telemetry::kMaxFaults + 1);
}

TEST_CASE("alerts: draw_alerts stacks one banner per alert from the top of the well", "[gui_rendering][alerts][draw]") {
  NullCanvas canvas;
  const Theme t = Theme::dark();
  Frame frame =
      make_frame({fault(FaultKind::OilLow), fault(FaultKind::FuelLow), fault(FaultKind::SenderOpen, Channel::Fuel)});
  const AlertList alerts = collect_alerts(&frame, true, 0);
  REQUIRE(alerts.size() == 3);

  c3::render::draw_alerts(canvas.vg(), kReferenceWell, t, Fonts{}, alerts);
  REQUIRE(canvas.calls().fills.size() == 3);
  for (std::size_t i = 0; i < 3; ++i) {
    expect_rect(canvas.calls().fills[i].bounds,
                {.x = 64.0F, .y = 124.0F + 116.0F * static_cast<float>(i), .w = 1472.0F, .h = 104.0F}, 0.05F);
  }
  CHECK(same_color(canvas.calls().fills[0].inner, c3::render::draw::alpha(t.alarm, 0.94F)));
  CHECK(same_color(canvas.calls().fills[1].inner, c3::render::draw::alpha(t.amber, 0.96F)));
  CHECK(same_color(canvas.calls().fills[2].inner, t.banner_neutral));
  CHECK(canvas.calls().strokes.size() == 1);  // only the neutral banner gets a hairline

  canvas.clear();
  c3::render::draw_alerts(canvas.vg(), kReferenceWell, t, Fonts{}, AlertList{});
  CHECK(canvas.calls().fills.empty());
}

TEST_CASE("alerts: draw_alert_chip draws nothing when clear and a right-aligned pill otherwise",
          "[gui_rendering][alerts][draw]") {
  NullCanvas canvas;
  const Theme t = Theme::dark();
  c3::render::draw_alert_chip(canvas.vg(), 1000.0F, 44.0F, t, Fonts{}, AlertList{});
  CHECK(canvas.calls().fills.empty());

  const AlertList lost = collect_alerts(nullptr, false, 0);
  c3::render::draw_alert_chip(canvas.vg(), 1000.0F, 44.0F, t, Fonts{}, lost);
  REQUIRE(canvas.calls().fills.size() == 1);
  // no font, so the caption measures 0 and the pill is just its padding, ending at right_x
  expect_rect(canvas.calls().fills[0].bounds, {.x = 956.0F, .y = 20.0F, .w = 44.0F, .h = 48.0F}, 0.05F);
  CHECK(same_color(canvas.calls().fills[0].inner, t.surface2));
  CHECK(canvas.calls().strokes.size() == 1);

  const Frame frame = make_frame({fault(FaultKind::OilLow)});
  canvas.clear();
  c3::render::draw_alert_chip(canvas.vg(), 1000.0F, 44.0F, t, Fonts{}, collect_alerts(&frame, true, 0));
  REQUIRE(canvas.calls().fills.size() == 1);
  CHECK(same_color(canvas.calls().fills[0].inner, c3::render::draw::alpha(t.alarm, 0.94F)));
  CHECK(canvas.calls().strokes.empty());
}

}  // namespace alerts_tests

// ===========================================================================
// console_scene.hpp
// ===========================================================================
namespace console_scene_tests {

using c3::render::classify_gauge;
using c3::render::ConsoleScene;
using c3::render::DampedValue;
using c3::render::GaugeState;
using c3::render::GaugeStatus;
using c3::render::Page;
using c3::render::SceneInput;
using c3::render::WallClock;
using c3::telemetry::kChannelCount;
using c3::telemetry::TelemetryView;

constexpr std::array<Channel, 4> kChannels = {Channel::Fuel, Channel::Oil, Channel::Coolant, Channel::Volt};

TEST_CASE("classify_gauge: without a live frame every channel is stale", "[gui_rendering][console_scene]") {
  const Frame frame = make_frame({fault(FaultKind::OilLow)});
  for (const Channel ch : kChannels) {
    const GaugeStatus none = classify_gauge(nullptr, false, ch);
    CHECK(none.state == GaugeState::Stale);
    CHECK(none.text == "NO DATA");
    CHECK(classify_gauge(nullptr, true, ch).state == GaugeState::Stale);
    CHECK(classify_gauge(&frame, false, ch).state == GaugeState::Stale);  // a stale frame's faults are not shown
  }
}

TEST_CASE("classify_gauge: a live frame with no faults is normal on every channel, whatever the values",
          "[gui_rendering][console_scene]") {
  Frame frame = make_frame();
  frame.oil_psi = 0.0F;
  frame.coolant_f = 400.0F;
  frame.fuel_fraction = 0.0F;
  frame.voltage_v = 0.0F;
  for (const Channel ch : kChannels) {
    const GaugeStatus s = classify_gauge(&frame, true, ch);
    CHECK(s.state == GaugeState::Ok);
    CHECK(s.text == "NORMAL");
  }
}

TEST_CASE("classify_gauge: each value fault warns its own channel only", "[gui_rendering][console_scene]") {
  struct Case {
    FaultKind kind;
    Channel channel;
    std::string_view text;
  };
  constexpr std::array<Case, 5> kCases = {{
      {FaultKind::OilLow, Channel::Oil, "LOW \xE2\x80\x94 STOP ENGINE"},
      {FaultKind::CltHigh, Channel::Coolant, "OVERHEAT"},
      {FaultKind::FuelLow, Channel::Fuel, "LOW FUEL"},
      {FaultKind::VoltLow, Channel::Volt, "LOW \xC2\xB7 CHECK CHARGING"},
      {FaultKind::VoltHigh, Channel::Volt, "HIGH \xC2\xB7 CHECK CHARGING"},
  }};
  for (const Case& c : kCases) {
    INFO(c3::telemetry::to_string(c.kind));
    const Frame frame = make_frame({fault(c.kind)});
    for (const Channel ch : kChannels) {
      const GaugeStatus s = classify_gauge(&frame, true, ch);
      if (ch == c.channel) {
        CHECK(s.state == GaugeState::Warn);
        CHECK(s.text == c.text);
      } else {
        CHECK(s.state == GaugeState::Ok);
      }
    }
  }
}

TEST_CASE("classify_gauge: low voltage is reported before high when the MCU sends both",
          "[gui_rendering][console_scene]") {
  const Frame frame = make_frame({fault(FaultKind::VoltHigh), fault(FaultKind::VoltLow)});
  CHECK(classify_gauge(&frame, true, Channel::Volt).text == "LOW \xC2\xB7 CHECK CHARGING");
}

TEST_CASE("classify_gauge: a sender fault names itself and outranks a value fault on the same channel",
          "[gui_rendering][console_scene]") {
  const Frame open = make_frame({fault(FaultKind::OilLow), fault(FaultKind::SenderOpen, Channel::Oil)});
  const GaugeStatus oil = classify_gauge(&open, true, Channel::Oil);
  CHECK(oil.state == GaugeState::SenderFault);
  CHECK(oil.text == "SENDER OPEN");
  CHECK(classify_gauge(&open, true, Channel::Coolant).state == GaugeState::Ok);

  const Frame shorted = make_frame({fault(FaultKind::SenderShort, Channel::Volt)});
  CHECK(classify_gauge(&shorted, true, Channel::Volt).state == GaugeState::SenderFault);
  CHECK(classify_gauge(&shorted, true, Channel::Volt).text == "SENDER SHORT");
  CHECK(classify_gauge(&shorted, true, Channel::Fuel).state == GaugeState::Ok);
}

TEST_CASE("DampedValue: the first target is adopted outright", "[gui_rendering][console_scene]") {
  DampedValue d;
  CHECK_FALSE(d.primed);
  d.step(41.2F, 7.0F, 0.016F);
  CHECK(d.primed);
  CHECK(d.value == 41.2F);
}

TEST_CASE("DampedValue: later steps move rate x dt of the remaining distance and never overshoot",
          "[gui_rendering][console_scene]") {
  DampedValue d;
  d.step(50.0F, 7.0F, 0.016F);
  d.step(60.0F, 7.0F, 0.1F);  // k = 0.7
  CHECK_THAT(d.value, WithinAbs(57.0, 1e-4));
  d.step(60.0F, 7.0F, 0.0F);  // no time passed: no movement
  CHECK_THAT(d.value, WithinAbs(57.0, 1e-4));
  d.step(60.0F, 7.0F, 1.0F);  // k clamps to 1: lands on the target
  CHECK_THAT(d.value, WithinAbs(60.0, 1e-4));
  d.step(60.0F, 7.0F, 5.0F);
  CHECK_THAT(d.value, WithinAbs(60.0, 1e-4));
  d.step(20.0F, 2.5F, 0.2F);  // k = 0.5, downward
  CHECK_THAT(d.value, WithinAbs(40.0, 1e-4));
}

// ---- the scene itself, drawn against the recording backend

[[nodiscard]] SceneInput live_input(const Frame* frame) noexcept {
  return SceneInput{.telemetry = TelemetryView{.frame = frame, .link = LinkState::Live, .age_ms = 0},
                    .carplay_connected = false,
                    .dock = {},
                    .clock = WallClock{.hour12 = 10, .minute = 7, .pm = false},
                    .dt_s = 0.016F,
                    .t_s = 1.0F};
}

TEST_CASE("ConsoleScene: construction derives every sub-layout from the panel layout",
          "[gui_rendering][console_scene]") {
  const Layout layout = Layout::compute(kPanel);
  const ConsoleScene scene(layout, Theme::dark(), Fonts{});
  CHECK(scene.layout().well == layout.well);
  CHECK(scene.dock_layout().dock == layout.dock);
  CHECK(scene.tabs_layout().pill.y == layout.tabs.y);
  CHECK_THAT(scene.tools_layout().rect(c3::render::Tool::Theme).right(), WithinAbs(layout.status.right(), 1e-3));
  CHECK(scene.camera_layout().well == layout.well);
  for (std::size_t i = 0; i < c3::render::kPageCount; ++i) {
    CHECK(scene.sheet_layout(static_cast<Page>(i)).kind == static_cast<Page>(i));
    CHECK(scene.sheet_layout(static_cast<Page>(i)).well == layout.well);
  }
}

TEST_CASE("ConsoleScene: a frame starts with the background and leaves the canvas state as it found it",
          "[gui_rendering][console_scene][draw]") {
  NullCanvas canvas;
  const Theme t = Theme::dark();
  ConsoleScene scene(Layout::compute(kPanel), t, Fonts{});
  const Frame frame = make_frame();
  const SceneInput in = live_input(&frame);

  scene.update(in);
  scene.draw(canvas.vg(), in);

  REQUIRE_FALSE(canvas.calls().fills.empty());
  expect_rect(canvas.calls().fills.front().bounds, {.x = 0.0F, .y = 0.0F, .w = 800.0F, .h = 1280.0F});
  CHECK(same_color(canvas.calls().fills.front().inner, t.bg));
  CHECK(canvas.transform_is_identity());
  CHECK(canvas.calls().triangle_calls == 0);  // no fonts loaded: no glyphs, and no crash either
  CHECK(canvas.calls().fills_with(t.well_bg) == 1);
  CHECK(canvas.calls().fills_with(t.ok) >= 1);  // the MCU link chip's dot while live
}

TEST_CASE("ConsoleScene: draws every telemetry state without a live frame", "[gui_rendering][console_scene][draw]") {
  NullCanvas canvas;
  const Theme t = Theme::light();
  ConsoleScene scene(Layout::compute(kPanel), t, Fonts{});

  SceneInput in = live_input(nullptr);
  in.telemetry.link = LinkState::NoData;
  scene.update(in);  // null frame: must not touch the needles or crash
  scene.draw(canvas.vg(), in);
  CHECK(canvas.transform_is_identity());
  CHECK(canvas.calls().fills_with(t.ok) == 0);  // nothing is up

  const Frame stale = make_frame();
  in.telemetry = TelemetryView{.frame = &stale, .link = LinkState::Stalled, .age_ms = 3000};
  canvas.clear();
  scene.draw(canvas.vg(), in);
  CHECK(canvas.transform_is_identity());
  CHECK(canvas.calls().fills_with(t.alarm) >= 1);  // the MCU chip's known-bad dot
}

TEST_CASE("ConsoleScene: draws a frame carrying every fault kind in both themes and unit systems",
          "[gui_rendering][console_scene][draw]") {
  Frame frame = make_frame({fault(FaultKind::OilLow), fault(FaultKind::CltHigh), fault(FaultKind::FuelLow),
                            fault(FaultKind::VoltLow), fault(FaultKind::VoltHigh),
                            fault(FaultKind::SenderOpen, Channel::Oil), fault(FaultKind::SenderShort, Channel::Fuel)});
  for (const Theme& t : {Theme::dark(), Theme::light()}) {
    NullCanvas canvas;
    ConsoleScene scene(Layout::compute(kPanel), t, Fonts{});
    SceneInput in = live_input(&frame);
    in.units = c3::render::Units{.temp = c3::render::TempUnit::Celsius, .pressure = c3::render::PressureUnit::Bar};
    in.dock.live = true;
    in.carplay_connected = true;
    for (int i = 0; i < 3; ++i) {
      scene.update(in);
    }
    scene.draw(canvas.vg(), in);
    CHECK(canvas.transform_is_identity());
    CHECK(canvas.calls().fills_with(c3::render::draw::alpha(t.alarm, 0.94F)) >= 1);  // alarm banners
  }
}

TEST_CASE("ConsoleScene: the well shows the camera over a sheet over CarPlay", "[gui_rendering][console_scene][draw]") {
  NullCanvas canvas;
  const Theme t = Theme::dark();
  ConsoleScene scene(Layout::compute(kPanel), t, Fonts{});
  const Frame frame = make_frame();
  SceneInput in = live_input(&frame);
  const Rect well = scene.layout().well;

  // CarPlay connecting: the well chrome, no sheet panel, no camera clip
  scene.draw(canvas.vg(), in);
  CHECK(canvas.calls().fills_with(t.sheet) == 0);
  CHECK(std::none_of(canvas.calls().fills.begin(), canvas.calls().fills.end(),
                     [](const FillCall& f) { return f.scissor.has_value(); }));

  // a settings page: the sheet panel fills the well
  in.well.page = Page::Units;
  canvas.clear();
  scene.draw(canvas.vg(), in);
  CHECK(canvas.calls().fills_with(t.sheet) == 1);

  // the camera wins over the page: clipped fills appear, the sheet panel does not
  in.well.camera = true;
  canvas.clear();
  scene.draw(canvas.vg(), in);
  CHECK(canvas.calls().fills_with(t.sheet) == 0);
  const auto clipped = std::find_if(canvas.calls().fills.begin(), canvas.calls().fills.end(),
                                    [](const FillCall& f) { return f.scissor.has_value(); });
  REQUIRE(clipped != canvas.calls().fills.end());
  expect_rect(*clipped->scissor, well);
  CHECK(canvas.transform_is_identity());
}

TEST_CASE("ConsoleScene: set_theme recolours the next frame", "[gui_rendering][console_scene][draw]") {
  NullCanvas canvas;
  ConsoleScene scene(Layout::compute(kPanel), Theme::dark(), Fonts{});
  const Frame frame = make_frame();
  const SceneInput in = live_input(&frame);
  scene.draw(canvas.vg(), in);
  CHECK(same_color(canvas.calls().fills.front().inner, Theme::dark().bg));
  scene.set_theme(Theme::light());
  canvas.clear();
  scene.draw(canvas.vg(), in);
  CHECK(same_color(canvas.calls().fills.front().inner, Theme::light().bg));
}

}  // namespace console_scene_tests

// ===========================================================================
// draw.hpp
// ===========================================================================
namespace draw_tests {

namespace draw = c3::render::draw;

TEST_CASE("draw: alpha replaces the alpha channel and leaves the colour alone", "[gui_rendering][draw]") {
  // Callers pass the alpha they want to see (alarm at 0.94, glow at 0), so the input alpha is
  // not a factor; a translucent theme token comes out at exactly the requested alpha.
  const Color c = draw::alpha(rgba(0xFF8000, 0.5F), 0.25F);
  CHECK_THAT(c.r, WithinAbs(1.0, 1e-6));
  CHECK_THAT(c.g, WithinAbs(128.0 / 255.0, 1e-6));
  CHECK(c.b == 0.0F);
  CHECK_THAT(c.a, WithinAbs(0.25, 1e-6));
  CHECK(draw::alpha(rgb(0xFF0000), 0.0F).a == 0.0F);
}

TEST_CASE("draw: State is a scope guard that cannot be copied or moved", "[gui_rendering][draw]") {
  STATIC_CHECK_FALSE(std::is_copy_constructible_v<draw::State>);
  STATIC_CHECK_FALSE(std::is_move_constructible_v<draw::State>);
  STATIC_CHECK_FALSE(std::is_copy_assignable_v<draw::State>);
  STATIC_CHECK_FALSE(std::is_move_assignable_v<draw::State>);
}

TEST_CASE("draw: State restores the transform when the scope ends", "[gui_rendering][draw]") {
  NullCanvas canvas;
  REQUIRE(canvas.transform_is_identity());
  {
    const draw::State st(canvas.vg());
    st.translate(10.0F, 20.0F);
    st.scale(2.0F);
    const std::array<float, 6> xf = canvas.transform();
    CHECK(xf[0] == 2.0F);
    CHECK(xf[3] == 2.0F);
    CHECK(xf[4] == 10.0F);
    CHECK(xf[5] == 20.0F);
    {
      const draw::State inner(canvas.vg());
      inner.translate(5.0F, 5.0F);  // in the scaled space: +10 px
      CHECK(canvas.transform()[4] == 20.0F);
    }
    CHECK(canvas.transform()[4] == 10.0F);
  }
  CHECK(canvas.transform_is_identity());
}

TEST_CASE("draw: rect_fill issues one fill with the rect's bounds and colour, unclipped", "[gui_rendering][draw]") {
  NullCanvas canvas;
  const Rect r{.x = 10.0F, .y = 20.0F, .w = 30.0F, .h = 40.0F};
  draw::rect_fill(canvas.vg(), r, rgb(0x123456));
  REQUIRE(canvas.calls().fills.size() == 1);
  expect_rect(canvas.calls().fills[0].bounds, r);
  CHECK(same_color(canvas.calls().fills[0].inner, rgb(0x123456)));
  CHECK_FALSE(canvas.calls().fills[0].scissor.has_value());
  CHECK(canvas.calls().strokes.empty());
}

TEST_CASE("draw: fills follow the current transform", "[gui_rendering][draw]") {
  NullCanvas canvas;
  {
    const draw::State st(canvas.vg());
    st.translate(100.0F, 200.0F);
    st.scale(0.5F);
    draw::rounded_fill(canvas.vg(), {.x = 0.0F, .y = 0.0F, .w = 40.0F, .h = 20.0F}, 4.0F, rgb(0xFFFFFF));
  }
  REQUIRE(canvas.calls().fills.size() == 1);
  expect_rect(canvas.calls().fills[0].bounds, {.x = 100.0F, .y = 200.0F, .w = 20.0F, .h = 10.0F}, 0.05F);
}

TEST_CASE("draw: clip confines later fills and nested clips intersect", "[gui_rendering][draw]") {
  NullCanvas canvas;
  const Rect big{.x = 0.0F, .y = 0.0F, .w = 800.0F, .h = 1280.0F};
  const Rect clip{.x = 100.0F, .y = 100.0F, .w = 200.0F, .h = 300.0F};
  {
    const draw::State st(canvas.vg());
    st.clip(clip);
    draw::rect_fill(canvas.vg(), big, rgb(0xFFFFFF));
    {
      const draw::State inner(canvas.vg());
      inner.clip({.x = 200.0F, .y = 0.0F, .w = 400.0F, .h = 250.0F});
      draw::rect_fill(canvas.vg(), big, rgb(0xFFFFFF));
    }
    draw::rect_fill(canvas.vg(), big, rgb(0xFFFFFF));
  }
  draw::rect_fill(canvas.vg(), big, rgb(0xFFFFFF));

  REQUIRE(canvas.calls().fills.size() == 4);
  REQUIRE(canvas.calls().fills[0].scissor.has_value());
  expect_rect(*canvas.calls().fills[0].scissor, clip, 0.05F);
  REQUIRE(canvas.calls().fills[1].scissor.has_value());
  expect_rect(*canvas.calls().fills[1].scissor, {.x = 200.0F, .y = 100.0F, .w = 100.0F, .h = 150.0F}, 0.05F);
  REQUIRE(canvas.calls().fills[2].scissor.has_value());
  expect_rect(*canvas.calls().fills[2].scissor, clip, 0.05F);
  CHECK_FALSE(canvas.calls().fills[3].scissor.has_value());
}

TEST_CASE("draw: rounded_stroke is a one-pixel hairline that stays inside its rect", "[gui_rendering][draw]") {
  NullCanvas canvas;
  const Rect r{.x = 10.0F, .y = 20.0F, .w = 100.0F, .h = 50.0F};
  draw::rounded_stroke(canvas.vg(), r, 8.0F, rgb(0xABCDEF));
  REQUIRE(canvas.calls().strokes.size() == 1);
  const StrokeCall& s = canvas.calls().strokes[0];
  CHECK(s.width == 1.0F);
  CHECK(same_color(s.inner, rgb(0xABCDEF)));
  // inset by half a pixel, so the outer edge lands on the rect edge; the AA fringe adds ≤ 0.5
  CHECK(inside(s.bounds, {.x = r.x - 0.5F, .y = r.y - 0.5F, .w = r.w + 1.0F, .h = r.h + 1.0F}));
  CHECK(s.bounds.w > r.w - 2.0F);
  CHECK(s.bounds.h > r.h - 2.0F);
  CHECK(canvas.calls().fills.empty());
}

TEST_CASE("draw: line and arc strokes carry their width and colour", "[gui_rendering][draw]") {
  NullCanvas canvas;
  draw::line(canvas.vg(), 0.0F, 10.0F, 100.0F, 10.0F, 2.0F, rgb(0x00FF00));
  draw::arc_stroke(canvas.vg(), 50.0F, 50.0F, 20.0F, 0.0F, 3.14159F, 4.0F, rgb(0x0000FF), draw::Cap::Round);
  REQUIRE(canvas.calls().strokes.size() == 2);
  CHECK(canvas.calls().strokes[0].width == 2.0F);
  CHECK(same_color(canvas.calls().strokes[0].inner, rgb(0x00FF00)));
  CHECK_THAT(canvas.calls().strokes[0].bounds.y, WithinAbs(9.0, 0.6));  // half the width plus fringe
  CHECK(canvas.calls().strokes[1].width == 4.0F);
  CHECK(same_color(canvas.calls().strokes[1].inner, rgb(0x0000FF)));
}

TEST_CASE("draw: circle_fill bounds are the disc's bounding box", "[gui_rendering][draw]") {
  NullCanvas canvas;
  draw::circle_fill(canvas.vg(), 100.0F, 100.0F, 25.0F, rgb(0xFFFFFF));
  REQUIRE(canvas.calls().fills.size() == 1);
  expect_rect(canvas.calls().fills[0].bounds, {.x = 75.0F, .y = 75.0F, .w = 50.0F, .h = 50.0F}, 0.05F);
}

TEST_CASE("draw: text with no font loaded draws nothing and measures zero", "[gui_rendering][draw]") {
  NullCanvas canvas;
  draw::text(canvas.vg(), -1, 24.0F, rgb(0xFFFFFF), draw::kLeftMiddle, 0.0F, 0.0F, "hello");
  draw::text_box(canvas.vg(), -1, 24.0F, rgb(0xFFFFFF), 0.0F, 0.0F, 100.0F, 1.4F, "hello world");
  CHECK(canvas.calls().triangle_calls == 0);
  CHECK(canvas.calls().fills.empty());
  CHECK(draw::text_width(canvas.vg(), -1, 24.0F, "hello") == 0.0F);
}

TEST_CASE("draw: the alignment constants combine one horizontal and one vertical anchor", "[gui_rendering][draw]") {
  STATIC_CHECK(draw::kLeftTop == (NVG_ALIGN_LEFT | NVG_ALIGN_TOP));
  STATIC_CHECK(draw::kCenterMiddle == (NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE));
  STATIC_CHECK(draw::kRightBaseline == (NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE));
}

}  // namespace draw_tests

// ===========================================================================
// icons.hpp
// ===========================================================================
namespace icons_tests {

using c3::render::draw_icon;
using c3::render::Icon;

constexpr int kLastIcon = static_cast<int>(Icon::Snowflake);

TEST_CASE("icons: every glyph is one stroke that stays inside its box in the given colour",
          "[gui_rendering][icons][draw]") {
  NullCanvas canvas;
  constexpr float kCx = 100.0F;
  constexpr float kCy = 150.0F;
  constexpr float kSize = 48.0F;
  // stroke is 2 of 24 units → 4 px; allow half of it plus the AA fringe outside the path
  constexpr float kSlack = 3.5F;
  const Color ink = rgb(0x9A9EA6);
  for (int i = 0; i <= kLastIcon; ++i) {
    const auto icon = static_cast<Icon>(i);
    INFO("icon " << i);
    canvas.clear();
    draw_icon(canvas.vg(), icon, kCx, kCy, kSize, ink);
    REQUIRE(canvas.calls().strokes.size() == 1);
    const StrokeCall& s = canvas.calls().strokes[0];
    CHECK(same_color(s.inner, ink));
    CHECK(inside(s.bounds, {.x = kCx - kSize / 2 - kSlack,
                            .y = kCy - kSize / 2 - kSlack,
                            .w = kSize + 2 * kSlack,
                            .h = kSize + 2 * kSlack}));
    CHECK(s.bounds.w > kSize / 4);
    CHECK(s.bounds.h > kSize / 4);
    CHECK(canvas.calls().fills.empty());
    CHECK(canvas.transform_is_identity());
  }
}

TEST_CASE("icons: the glyph scales with size and the stroke with it", "[gui_rendering][icons][draw]") {
  NullCanvas canvas;
  // Close is the X from (6,6) to (18,18) of the 24-unit box: half the size, centred
  draw_icon(canvas.vg(), Icon::Close, 200.0F, 200.0F, 96.0F, rgb(0xFFFFFF));
  REQUIRE(canvas.calls().strokes.size() == 1);
  const StrokeCall& s = canvas.calls().strokes[0];
  CHECK_THAT(s.width, WithinAbs(8.0, 1e-4));  // 2 of 24 units at 96 px: the stroke scales with the glyph
  CHECK_THAT(s.bounds.x, WithinAbs(200.0 - 24.0, 7.0));
  CHECK_THAT(s.bounds.right(), WithinAbs(200.0 + 24.0, 7.0));
  CHECK_THAT(s.bounds.cx(), WithinAbs(200.0, 0.5));
  CHECK_THAT(s.bounds.cy(), WithinAbs(200.0, 0.5));
}

TEST_CASE("icons: a custom stroke weight is passed through", "[gui_rendering][icons][draw]") {
  NullCanvas canvas;
  draw_icon(canvas.vg(), Icon::Gear, 50.0F, 50.0F, 24.0F, rgb(0xFFFFFF), 3.0F);
  REQUIRE(canvas.calls().strokes.size() == 1);
  CHECK_THAT(canvas.calls().strokes[0].width, WithinAbs(3.0, 1e-5));
}

}  // namespace icons_tests

// ===========================================================================
// brand.hpp
// ===========================================================================
namespace brand_tests {

using c3::render::brand_width;
using c3::render::draw_brand_mark;
using c3::render::kBrandH;
using c3::render::kBrandW;

TEST_CASE("brand: brand_width keeps the mark's 408:262 aspect", "[gui_rendering][brand]") {
  STATIC_CHECK(brand_width(kBrandH) == kBrandW);
  STATIC_CHECK(brand_width(131.0F) == 204.0F);
  CHECK_THAT(brand_width(46.0F), WithinAbs(46.0 * 408.0 / 262.0, 1e-3));
}

TEST_CASE("brand: draw_brand_mark fills once, in the given colour, within its box", "[gui_rendering][brand][draw]") {
  NullCanvas canvas;
  const Color purple = rgba(0x9988CC, 0.95F);
  draw_brand_mark(canvas.vg(), 10.0F, 20.0F, 131.0F, purple);
  REQUIRE(canvas.calls().fills.size() == 1);
  const FillCall& f = canvas.calls().fills[0];
  CHECK(same_color(f.inner, purple));
  const Rect box{.x = 10.0F, .y = 20.0F, .w = brand_width(131.0F), .h = 131.0F};
  CHECK(inside(f.bounds, box, 0.5F));
  CHECK(f.bounds.w > 0.9F * box.w);
  CHECK(f.bounds.h > 0.95F * box.h);
  CHECK(canvas.calls().strokes.empty());
  CHECK(canvas.transform_is_identity());
}

}  // namespace brand_tests

// ===========================================================================
// fonts.hpp
// ===========================================================================
namespace fonts_tests {

using c3::render::FontsResult;
using c3::render::load_fonts;

TEST_CASE("fonts: a null context is refused with a reason", "[gui_rendering][fonts]") {
  const FontsResult r = load_fonts(nullptr, "/nonexistent");
  CHECK_FALSE(r.fonts.has_value());
  CHECK(r.error == "no NanoVG context");
}

TEST_CASE("fonts: a directory without the fonts names the first missing file", "[gui_rendering][fonts]") {
  NullCanvas canvas;
  const ScratchDir dir;
  const FontsResult r = load_fonts(canvas.vg(), dir.path());
  CHECK_FALSE(r.fonts.has_value());
  CHECK(r.error == "cannot load font " + (dir.path() / "Inter-Regular.ttf").string());
}

TEST_CASE("fonts: a partial set names the file that stopped the load", "[gui_rendering][fonts]") {
  const auto source = system_font();
  if (!source) {
    SKIP("no TrueType font found on this host to stand in for the shipped ones");
  }
  NullCanvas canvas;
  const ScratchDir dir;
  populate_fonts(dir, *source, kFontFiles.size() - 1);
  const FontsResult r = load_fonts(canvas.vg(), dir.path());
  CHECK_FALSE(r.fonts.has_value());
  CHECK(r.error == "cannot load font " + (dir.path() / "Barlow-SemiBold.ttf").string());
}

TEST_CASE("fonts: the full set loads into six distinct handles that measure and draw text",
          "[gui_rendering][fonts][draw]") {
  const auto source = system_font();
  if (!source) {
    SKIP("no TrueType font found on this host to stand in for the shipped ones");
  }
  NullCanvas canvas;
  const ScratchDir dir;
  populate_fonts(dir, *source, kFontFiles.size());
  const FontsResult r = load_fonts(canvas.vg(), dir.path());
  REQUIRE(r.fonts.has_value());
  CHECK(r.error.empty());
  const Fonts& f = *r.fonts;
  const std::array<int, 6> handles = {f.ui_regular, f.ui_medium,  f.ui_semibold,
                                      f.num_light,  f.num_medium, f.num_semibold};
  for (std::size_t i = 0; i < handles.size(); ++i) {
    CHECK(handles[i] >= 0);
    for (std::size_t j = i + 1; j < handles.size(); ++j) {
      CHECK(handles[i] != handles[j]);
    }
  }

  namespace draw = c3::render::draw;
  const float narrow = draw::text_width(canvas.vg(), f.ui_medium, 24.0F, "I");
  const float wide = draw::text_width(canvas.vg(), f.ui_medium, 24.0F, "WWWW");
  CHECK(narrow > 0.0F);
  CHECK(wide > 4.0F * narrow);
  CHECK(draw::text_width(canvas.vg(), f.ui_medium, 48.0F, "WWWW") > 1.9F * wide);
  CHECK(draw::text_width(canvas.vg(), f.ui_medium, 24.0F, "WWWW", 10.0F) > wide + 29.0F);  // tracking

  draw::text(canvas.vg(), f.num_medium, 30.0F, rgb(0xFFFFFF), draw::kLeftMiddle, 10.0F, 10.0F, "14.1");
  CHECK(canvas.calls().triangle_calls >= 1);

  // and the alert chip now sizes itself to its caption
  const Theme t = Theme::dark();
  canvas.clear();
  c3::render::draw_alert_chip(canvas.vg(), 1000.0F, 44.0F, t, f, c3::render::collect_alerts(nullptr, false, 0));
  REQUIRE(canvas.calls().fills.size() == 1);
  CHECK(canvas.calls().fills[0].bounds.w > 44.0F);
  CHECK_THAT(canvas.calls().fills[0].bounds.right(), WithinAbs(1000.0, 0.05));
}

}  // namespace fonts_tests

// ===========================================================================
// widgets.hpp
// ===========================================================================
namespace widgets_tests {

namespace widgets = c3::render::widgets;
using c3::render::Icon;
using widgets::SliderStyle;

TEST_CASE("widgets: split_segments tiles the inner width with equal buttons", "[gui_rendering][widgets]") {
  const Rect group{.x = 0.0F, .y = 0.0F, .w = 960.0F, .h = 96.0F};
  std::array<Rect, 3> three{};
  widgets::split_segments(group, 1.0F, three);
  expect_rect(three[0], {.x = 6.0F, .y = 6.0F, .w = 312.0F, .h = 84.0F});
  expect_rect(three[1], {.x = 324.0F, .y = 6.0F, .w = 312.0F, .h = 84.0F});
  expect_rect(three[2], {.x = 642.0F, .y = 6.0F, .w = 312.0F, .h = 84.0F});
  CHECK_THAT(three[2].right(), WithinAbs(954.0, 1e-3));

  std::array<Rect, 5> five{};
  widgets::split_segments(group, 0.5F, five);  // padding and gap halve, buttons widen
  CHECK(five[0].x == 3.0F);
  CHECK(five[0].y == 3.0F);
  CHECK(five[0].h == 90.0F);
  CHECK_THAT(five[0].w, WithinAbs((960.0 - 6.0 - 12.0) / 5.0, 1e-3));
  CHECK_THAT(five[4].right(), WithinAbs(957.0, 1e-3));
  for (std::size_t i = 1; i < five.size(); ++i) {
    CHECK_THAT(five[i].x - five[i - 1].right(), WithinAbs(3.0, 1e-3));
  }

  std::array<Rect, 1> one{};
  widgets::split_segments(group, 1.0F, one);
  expect_rect(one[0], {.x = 6.0F, .y = 6.0F, .w = 948.0F, .h = 84.0F});
}

TEST_CASE("widgets: slider_fraction maps the drawn thumb travel to 0..1 and clamps outside it",
          "[gui_rendering][widgets]") {
  const Rect r{.x = 100.0F, .y = 0.0F, .w = 400.0F, .h = 72.0F};
  const SliderStyle standard{};
  CHECK(widgets::slider_fraction(r, 1.0F, standard, 126.0F) == 0.0F);  // half the 52-unit thumb in
  CHECK(widgets::slider_fraction(r, 1.0F, standard, 474.0F) == 1.0F);
  CHECK_THAT(widgets::slider_fraction(r, 1.0F, standard, 300.0F), WithinAbs(0.5, 1e-6));
  CHECK(widgets::slider_fraction(r, 1.0F, standard, 0.0F) == 0.0F);
  CHECK(widgets::slider_fraction(r, 1.0F, standard, 1000.0F) == 1.0F);
  CHECK_THAT(widgets::slider_fraction(r, 1.0F, standard, 213.0F), WithinAbs(0.25, 1e-6));
}

TEST_CASE("widgets: slider_fraction follows the style's thumb size and the scale", "[gui_rendering][widgets]") {
  const Rect r{.x = 100.0F, .y = 0.0F, .w = 400.0F, .h = 72.0F};
  const SliderStyle big{.track_h = 20.0F, .thumb_d = 60.0F, .fill_from_start = true};  // the volume slider
  CHECK(widgets::slider_fraction(r, 1.0F, big, 130.0F) == 0.0F);
  CHECK(widgets::slider_fraction(r, 1.0F, big, 470.0F) == 1.0F);
  CHECK(widgets::slider_fraction(r, 1.0F, big, 126.0F) == 0.0F);  // still clamped, not negative

  const SliderStyle standard{};
  CHECK(widgets::slider_fraction(r, 0.5F, standard, 113.0F) == 0.0F);  // a 26 px thumb at half scale
  CHECK(widgets::slider_fraction(r, 0.5F, standard, 487.0F) == 1.0F);
}

TEST_CASE("widgets: slider_fraction reads 0 on a rect too narrow for the thumb", "[gui_rendering][widgets]") {
  const Rect narrow{.x = 0.0F, .y = 0.0F, .w = 40.0F, .h = 72.0F};
  CHECK(widgets::slider_fraction(narrow, 1.0F, SliderStyle{}, 20.0F) == 0.0F);
  CHECK(widgets::slider_fraction(narrow, 1.0F, SliderStyle{}, 1000.0F) == 0.0F);
}

TEST_CASE("widgets: slider draws the thumb where slider_fraction would read it back",
          "[gui_rendering][widgets][draw]") {
  NullCanvas canvas;
  const Theme t = Theme::dark();
  const Rect r{.x = 100.0F, .y = 0.0F, .w = 400.0F, .h = 72.0F};
  const SliderStyle big{.track_h = 20.0F, .thumb_d = 60.0F, .fill_from_start = true};

  widgets::slider(canvas.vg(), t, 1.0F, r, 0.5F, big, t.sheet);
  // the ember fill runs from the start of the travel to the thumb centre
  const auto ember = std::find_if(canvas.calls().fills.begin(), canvas.calls().fills.end(),
                                  [&](const FillCall& f) { return same_color(f.inner, t.ember); });
  REQUIRE(ember != canvas.calls().fills.end());
  expect_rect(ember->bounds, {.x = 130.0F, .y = 26.0F, .w = 170.0F, .h = 20.0F}, 0.05F);
  CHECK(widgets::slider_fraction(r, 1.0F, big, ember->bounds.right()) == 0.5F);
  // the thumb disc (ink) is centred there too
  const auto thumb = std::find_if(canvas.calls().fills.begin(), canvas.calls().fills.end(),
                                  [&](const FillCall& f) { return same_color(f.inner, t.ink); });
  REQUIRE(thumb != canvas.calls().fills.end());
  CHECK_THAT(thumb->bounds.cx(), WithinAbs(300.0, 0.05));
  CHECK_THAT(thumb->bounds.cy(), WithinAbs(36.0, 0.05));

  canvas.clear();
  widgets::slider(canvas.vg(), t, 1.0F, r, 0.0F, big, t.sheet);
  CHECK(canvas.calls().fills_with(t.ember) == 0);  // nothing to fill at zero

  canvas.clear();
  widgets::slider(canvas.vg(), t, 1.0F, r, 0.5F, SliderStyle{.centre_tick = true}, t.sheet);
  CHECK(canvas.calls().fills_with(t.ember) == 0);
  CHECK(canvas.calls().fills_with(t.ink3) == 1);  // the centre tick
  CHECK(canvas.transform_is_identity());
}

TEST_CASE("widgets: segment_button fills ember when on, surface2 when pressed, nothing otherwise",
          "[gui_rendering][widgets][draw]") {
  NullCanvas canvas;
  const Theme t = Theme::dark();
  const Rect r{.x = 0.0F, .y = 0.0F, .w = 312.0F, .h = 84.0F};
  widgets::segment_button(canvas.vg(), Fonts{}, t, 1.0F, r, "Vent", true, true);
  CHECK(canvas.calls().fills_with(t.ember) == 1);
  CHECK(canvas.calls().fills_with(t.surface2) == 0);
  canvas.clear();
  widgets::segment_button(canvas.vg(), Fonts{}, t, 1.0F, r, "Vent", false, true);
  CHECK(canvas.calls().fills_with(t.surface2) == 1);
  canvas.clear();
  widgets::segment_button(canvas.vg(), Fonts{}, t, 1.0F, r, "Vent", false, false);
  CHECK(canvas.calls().fills.empty());
}

TEST_CASE("widgets: toggle and icon_button take the accent only when on", "[gui_rendering][widgets][draw]") {
  NullCanvas canvas;
  const Theme t = Theme::dark();
  const Rect r{.x = 0.0F, .y = 0.0F, .w = 448.0F, .h = 85.0F};
  const Icon icon = Icon::Snowflake;
  widgets::toggle(canvas.vg(), Fonts{}, t, 1.0F, r, "A/C", "ON", true, false, &icon);
  CHECK(canvas.calls().fills_with(t.ember) == 1);
  CHECK(canvas.calls().strokes.size() == 2);  // the hairline and the icon
  canvas.clear();
  widgets::toggle(canvas.vg(), Fonts{}, t, 1.0F, r, "A/C", "OFF", false, true);
  CHECK(canvas.calls().fills_with(t.surface2) == 1);
  CHECK(canvas.calls().strokes.size() == 1);  // no icon requested
  canvas.clear();
  widgets::toggle(canvas.vg(), Fonts{}, t, 1.0F, r, "A/C", "OFF", false, false);
  CHECK(canvas.calls().fills_with(t.surface) == 1);

  const Rect button{.x = 0.0F, .y = 0.0F, .w = 64.0F, .h = 64.0F};
  canvas.clear();
  widgets::icon_button(canvas.vg(), t, button, Icon::Sun, true, false);
  CHECK(canvas.calls().fills_with(t.ember) == 1);
  expect_rect(canvas.calls().fills[0].bounds, button, 0.05F);
  canvas.clear();
  widgets::icon_button(canvas.vg(), t, button, Icon::Sun, false, false);
  CHECK(canvas.calls().fills_with(t.surface) == 1);
  CHECK(canvas.calls().fills_with(t.ember) == 0);
}

TEST_CASE("widgets: swatch is a disc in its colour with a selection ring only when on",
          "[gui_rendering][widgets][draw]") {
  NullCanvas canvas;
  const Theme t = Theme::dark();
  const Rect r{.x = 0.0F, .y = 0.0F, .w = 84.0F, .h = 84.0F};
  widgets::swatch(canvas.vg(), t, r, rgb(0x38BDF8), false);
  CHECK(canvas.calls().fills_with(rgb(0x38BDF8)) == 1);
  CHECK(canvas.calls().strokes.size() == 1);
  canvas.clear();
  widgets::swatch(canvas.vg(), t, r, rgb(0x38BDF8), true);
  CHECK(canvas.calls().strokes.size() == 2);
  CHECK(same_color(canvas.calls().strokes[1].inner, t.ink));
  CHECK(canvas.calls().strokes[1].bounds.w > r.w);  // the ring sits outside the disc
}

TEST_CASE("widgets: hue_track paints six spectrum segments under a clip and a thumb in the given colour",
          "[gui_rendering][widgets][draw]") {
  NullCanvas canvas;
  const Theme t = Theme::dark();
  const Rect r{.x = 100.0F, .y = 0.0F, .w = 400.0F, .h = 72.0F};
  widgets::hue_track(canvas.vg(), 1.0F, r, 0.25F, t.ember, t.sheet);
  std::size_t clipped = 0;
  for (const FillCall& f : canvas.calls().fills) {
    if (f.scissor.has_value()) {
      ++clipped;
      expect_rect(*f.scissor, {.x = 126.0F, .y = 28.0F, .w = 348.0F, .h = 16.0F}, 0.05F);
    }
  }
  CHECK(clipped == 6);
  CHECK(canvas.calls().fills_with(t.ember) == 1);  // the thumb
  CHECK(canvas.transform_is_identity());
}

}  // namespace widgets_tests

}  // namespace
