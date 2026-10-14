// render_loop.cpp — one tick is one frame: ingest, route touch, update, draw, present.
//
// The display is a NanoVG context over a renderer that discards everything, so the whole
// scene really draws; the input source is scripted per tick; telemetry comes from a fake
// source. The loop runs against the real clock at 60 Hz, so each case lasts a few frames.
#include "app/dock_controller.hpp"
#include "app/render_loop.hpp"
#include "app/settings_store.hpp"
#include "app/well_controller.hpp"
#include "audio/audio_state.hpp"
#include "logging/log.hpp"
#include "platform/debug_frame_capture.hpp"
#include "platform/display_backend.hpp"
#include "platform/input_source.hpp"
#include "platform/watchdog.hpp"
#include "render/console_scene.hpp"
#include "render/dock.hpp"
#include "render/layout.hpp"
#include "render/settings.hpp"
#include "render/sheet.hpp"
#include "render/status_tools.hpp"
#include "render/tabs.hpp"
#include "render/theme.hpp"
#include "telemetry/csv_recorder.hpp"
#include "telemetry/frame.hpp"
#include "telemetry/sensor_source.hpp"
#include "vehicle/vehicle_commands.hpp"
#include "vehicle/vehicle_types.hpp"

#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

using c3::app::DockController;
using c3::app::RenderLoopOptions;
using c3::app::run_render_loop;
using c3::app::WellController;
using c3::platform::PresentResult;
using c3::platform::Size;
using c3::platform::TouchEvent;
using c3::platform::TouchPhase;
using c3::render::ConsoleScene;
using c3::render::DockControl;
using c3::render::Layout;
using c3::render::Page;
using c3::render::Rect;
using c3::render::Tab;
using c3::render::Theme;
using c3::render::Tool;
using c3::telemetry::Frame;
using c3::telemetry::LinkState;
using c3::vehicle::HvacBlower;
using c3::vehicle::HvacMode;
using c3::vehicle::HvacState;
using c3::vehicle::LampState;
using namespace std::chrono_literals;

namespace {

namespace fs = std::filesystem;
constexpr Size kPanel{.width = 800, .height = 1280};

class ScratchDir {
public:
  ScratchDir() {
    static int counter = 0;
    path_ = fs::temp_directory_path() / ("c3_rloop_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
    fs::create_directories(path_);
  }
  ~ScratchDir() {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }
  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;
  ScratchDir(ScratchDir&&) = delete;
  ScratchDir& operator=(ScratchDir&&) = delete;

  [[nodiscard]] std::string path_of(std::string_view name) const { return (path_ / name).string(); }

private:
  fs::path path_;
};

/// Discards log lines (the loop logs its start, stop and touches).
class QuietLog {
public:
  QuietLog() : sink_(c3::logging::Sink{.context = nullptr, .emit = &QuietLog::discard}) {}

private:
  static void discard(void*, std::string_view) noexcept {}
  c3::logging::ScopedSink sink_;
};

// ---------------------------------------------------------------------------
// A display whose NanoVG context tessellates on the CPU and renders nowhere.
// ---------------------------------------------------------------------------

class NullDisplay final : public c3::platform::DisplayBackend {
public:
  int fail_begin_at = 0;  // 1-based frame index; 0 = never
  int fail_end_at = 0;
  int begun = 0;
  int presented = 0;

  NullDisplay() {
    NVGparams p{};
    p.userPtr = this;
    p.edgeAntiAlias = 1;
    p.renderCreate = [](void*) { return 1; };
    p.renderCreateTexture = [](void* u, int, int w, int h, int, const unsigned char*) {
      auto& self = *static_cast<NullDisplay*>(u);
      const int id = self.next_texture_++;
      self.textures_[id] = {w, h};
      return id;
    };
    p.renderDeleteTexture = [](void* u, int id) {
      return static_cast<NullDisplay*>(u)->textures_.erase(id) > 0 ? 1 : 0;
    };
    p.renderUpdateTexture = [](void*, int, int, int, int, int, const unsigned char*) { return 1; };
    p.renderGetTextureSize = [](void* u, int id, int* w, int* h) {
      auto& self = *static_cast<NullDisplay*>(u);
      const auto it = self.textures_.find(id);
      if (it == self.textures_.end()) {
        return 0;
      }
      *w = it->second.first;
      *h = it->second.second;
      return 1;
    };
    p.renderViewport = [](void*, float, float, float) {};
    p.renderCancel = [](void*) {};
    p.renderFlush = [](void*) {};
    p.renderFill = [](void*, NVGpaint*, NVGcompositeOperationState, NVGscissor*, float, const float*, const NVGpath*,
                      int) {};
    p.renderStroke = [](void*, NVGpaint*, NVGcompositeOperationState, NVGscissor*, float, float, const NVGpath*, int) {
    };
    p.renderTriangles = [](void*, NVGpaint*, NVGcompositeOperationState, NVGscissor*, const NVGvertex*, int, float) {};
    p.renderDelete = [](void*) {};
    vg_ = nvgCreateInternal(&p);
    REQUIRE(vg_ != nullptr);
  }
  ~NullDisplay() override { nvgDeleteInternal(vg_); }
  NullDisplay(const NullDisplay&) = delete;
  NullDisplay& operator=(const NullDisplay&) = delete;
  NullDisplay(NullDisplay&&) = delete;
  NullDisplay& operator=(NullDisplay&&) = delete;

  [[nodiscard]] Size size() const noexcept override { return kPanel; }
  [[nodiscard]] float dpi() const noexcept override { return 264.0F; }
  [[nodiscard]] NVGcontext* context() const noexcept override { return vg_; }
  [[nodiscard]] NVGcontext* begin_frame() noexcept override {
    ++begun;
    if (begun == fail_begin_at) {
      return nullptr;
    }
    nvgBeginFrame(vg_, static_cast<float>(kPanel.width), static_cast<float>(kPanel.height), 1.0F);
    return vg_;
  }
  [[nodiscard]] PresentResult end_frame() noexcept override {
    nvgEndFrame(vg_);
    if (begun == fail_end_at) {
      return PresentResult::Failed;
    }
    ++presented;
    return PresentResult::Presented;
  }

private:
  NVGcontext* vg_ = nullptr;
  std::unordered_map<int, std::pair<int, int>> textures_;
  int next_texture_ = 1;
};

/// Touch events scripted per tick; quits after `quit_after_ticks` ticks.
class ScriptedInput final : public c3::platform::InputSource {
public:
  int quit_after_ticks = 5;
  std::map<int, std::deque<TouchEvent>> script;  // 1-based tick -> events delivered that tick
  mutable int ticks = 0;                         // counted in quit_requested(), which the loop calls first

  [[nodiscard]] bool poll(TouchEvent& out) noexcept override {
    auto it = script.find(ticks);
    if (it == script.end() || it->second.empty()) {
      return false;
    }
    out = it->second.front();
    it->second.pop_front();
    return true;
  }
  [[nodiscard]] bool quit_requested() const noexcept override {
    ++ticks;  // the loop asks once at the start of every tick
    return ticks > quit_after_ticks;
  }

  void tap_at(int tick, float x, float y) {
    script[tick].push_back(TouchEvent{.phase = TouchPhase::Down, .id = 1, .x = x, .y = y});
    script[tick].push_back(TouchEvent{.phase = TouchPhase::Up, .id = 1, .x = x, .y = y});
  }
  void tap_at(int tick, const Rect& r) { tap_at(tick, r.cx(), r.cy()); }
};

class FakeSource final : public c3::telemetry::SensorSource {
public:
  std::deque<Frame> frames;
  [[nodiscard]] std::optional<Frame> poll() noexcept override {
    if (frames.empty()) {
      return std::nullopt;
    }
    const Frame f = frames.front();
    frames.pop_front();
    return f;
  }
  [[nodiscard]] c3::telemetry::SourceHealth health() const noexcept override {
    return {.state = LinkState::Live, .frames_ok = 1};
  }
};

class RecordingSink final : public c3::vehicle::CommandSink {
public:
  std::vector<HvacState> hvac;
  std::vector<LampState> lamps;
  [[nodiscard]] c3::vehicle::SubmitResult send_hvac(const HvacState& s) noexcept override {
    hvac.push_back(s);
    return c3::vehicle::SubmitResult::Accepted;
  }
  [[nodiscard]] c3::vehicle::SubmitResult send_lamp(const LampState& s) noexcept override {
    lamps.push_back(s);
    return c3::vehicle::SubmitResult::Accepted;
  }
  [[nodiscard]] c3::vehicle::CommandStatus status(c3::vehicle::CommandKind) const noexcept override {
    return c3::vehicle::CommandStatus::Confirmed;
  }
};

class NullAudio final : public c3::audio::AudioSink {
public:
  void apply(const c3::audio::AudioState&) noexcept override {}
};

class FakeCapture final : public c3::platform::DebugFrameCapture {
public:
  std::vector<std::string> requested;
  bool accept = true;
  [[nodiscard]] bool request_capture(std::string_view ppm_path) noexcept override {
    requested.emplace_back(ppm_path);
    return accept;
  }
};

class CountingWatchdog final : public c3::platform::Watchdog {
public:
  int kicks = 0;
  void kick() noexcept override { ++kicks; }
};

/// A live frame whose relays report the given state.
[[nodiscard]] Frame live_frame(const HvacState& h = {}, const LampState& l = {}) noexcept {
  Frame f{.t_ms = 1000, .fuel_fraction = 0.62F, .oil_psi = 41.2F, .coolant_f = 196.4F, .voltage_v = 14.1F};
  f.relays = c3::vehicle::RelayState{.blower = h.blower,
                                     .mode = h.mode,
                                     .ac_enabled = h.ac_enabled,
                                     .headlights_on = l.headlights_on,
                                     .lamp_doors_open = l.lamp_doors_open,
                                     .highbeams_on = l.highbeams_on};
  return f;
}

/// Everything run_render_loop needs, built around the 1x panel.
struct Rig {
  QuietLog quiet;
  Layout layout = Layout::compute(kPanel);
  NullDisplay display;
  ScriptedInput input;
  FakeSource source;
  ConsoleScene scene{layout, Theme::dark(), c3::render::Fonts{}};
  RecordingSink sink;
  DockController controls{sink};
  NullAudio audio;
  WellController surfaces{layout, c3::render::Settings{}, nullptr, audio};
  RenderLoopOptions opts;
  std::atomic<bool> stop{false};

  [[nodiscard]] int run() { return run_render_loop(display, input, source, scene, controls, surfaces, opts, stop); }

  [[nodiscard]] Rect tab(Tab t) const { return c3::render::TabsLayout::compute(layout.tabs).rect(t); }
  [[nodiscard]] Rect dock(DockControl c) const { return c3::render::DockLayout::compute(layout.dock).rect(c); }
  [[nodiscard]] Rect theme_tool() const { return c3::render::ToolsLayout::compute(layout.status).rect(Tool::Theme); }
};

// ---------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("run_render_loop: draws one frame per tick until the input asks to quit", "[render_loop]") {
  Rig rig;
  rig.input.quit_after_ticks = 5;
  CountingWatchdog dog;
  rig.opts.watchdog = &dog;
  CHECK(rig.run() == 0);
  CHECK(rig.display.begun == 5);
  CHECK(rig.display.presented == 5);
  CHECK(dog.kicks == 5);  // once per presented frame
}

TEST_CASE("run_render_loop: the stop flag ends the loop cleanly", "[render_loop]") {
  Rig rig;
  rig.input.quit_after_ticks = 100000;
  std::thread stopper([&] {
    std::this_thread::sleep_for(80ms);
    rig.stop.store(true);
  });
  CHECK(rig.run() == 0);
  stopper.join();
  CHECK(rig.display.presented >= 2);
  CHECK(rig.display.presented == rig.display.begun);
}

TEST_CASE("run_render_loop: a frame that cannot begin stops the loop with a failure", "[render_loop]") {
  Rig rig;
  rig.input.quit_after_ticks = 100;
  rig.display.fail_begin_at = 3;
  CHECK(rig.run() == 1);
  CHECK(rig.display.begun == 3);
  CHECK(rig.display.presented == 2);
}

TEST_CASE("run_render_loop: a frame that cannot be presented stops the loop with a failure", "[render_loop]") {
  Rig rig;
  rig.input.quit_after_ticks = 100;
  rig.display.fail_end_at = 2;
  CountingWatchdog dog;
  rig.opts.watchdog = &dog;
  CHECK(rig.run() == 1);
  CHECK(rig.display.begun == 2);
  CHECK(rig.display.presented == 1);
  CHECK(dog.kicks == 1);  // the failed tick did not kick
}

// ---------------------------------------------------------------------------
// screenshot mode
// ---------------------------------------------------------------------------

TEST_CASE("run_render_loop: screenshot mode captures the first frame past the delay and exits", "[render_loop]") {
  Rig rig;
  FakeCapture capture;
  rig.input.quit_after_ticks = 100000;
  rig.opts.screenshot_path = "/tmp/c3-panel.ppm";
  rig.opts.screenshot_after_s = 0.0F;
  rig.opts.capture = &capture;
  CHECK(rig.run() == 0);
  CHECK(capture.requested == std::vector<std::string>{"/tmp/c3-panel.ppm"});
  CHECK(rig.display.presented == 1);  // the captured frame is presented, then the loop exits
}

TEST_CASE("run_render_loop: the screenshot delay is honoured", "[render_loop]") {
  Rig rig;
  FakeCapture capture;
  rig.input.quit_after_ticks = 100000;
  rig.opts.screenshot_path = "/tmp/c3-panel.ppm";
  rig.opts.screenshot_after_s = 0.1F;
  rig.opts.capture = &capture;
  const auto started = std::chrono::steady_clock::now();
  CHECK(rig.run() == 0);
  CHECK(std::chrono::steady_clock::now() - started >= 90ms);
  CHECK(capture.requested.size() == 1);
  CHECK(rig.display.presented >= 2);
}

TEST_CASE("run_render_loop: a refused capture is logged and the run still ends after that frame", "[render_loop]") {
  Rig rig;
  FakeCapture capture;
  capture.accept = false;
  rig.input.quit_after_ticks = 100000;
  rig.opts.screenshot_path = "/tmp/c3-panel.ppm";
  rig.opts.screenshot_after_s = 0.0F;
  rig.opts.capture = &capture;
  CHECK(rig.run() == 0);
  CHECK(capture.requested.size() == 1);
  CHECK(rig.display.presented == 1);
}

TEST_CASE("run_render_loop: a screenshot path without a capture hook runs normally", "[render_loop]") {
  Rig rig;
  rig.input.quit_after_ticks = 3;
  rig.opts.screenshot_path = "/tmp/c3-panel.ppm";
  rig.opts.capture = nullptr;
  CHECK(rig.run() == 0);
  CHECK(rig.display.presented == 3);
}

// ---------------------------------------------------------------------------
// telemetry
// ---------------------------------------------------------------------------

TEST_CASE("run_render_loop: frames reach the recorder and the dock sees the relays live", "[render_loop]") {
  Rig rig;
  const ScratchDir dir;
  auto opened = c3::telemetry::CsvRecorder::open(dir.path_of("drive.csv"));
  REQUIRE(opened.recorder != nullptr);
  rig.opts.recorder = opened.recorder.get();
  rig.input.quit_after_ticks = 4;
  const HvacState hvac{.mode = HvacMode::Floor, .blower = HvacBlower::L2, .ac_enabled = true};
  rig.source.frames.push_back(live_frame(hvac));
  rig.source.frames.push_back(live_frame(hvac));

  CHECK(rig.run() == 0);
  CHECK(opened.recorder->frames_written() == 2);
  const c3::render::DockModel dock = rig.controls.model();
  CHECK(dock.live);
  CHECK(dock.hvac == hvac);
  CHECK(dock.doors_known);
}

TEST_CASE("run_render_loop: without frames the dock stays unknown and the well stays on CarPlay", "[render_loop]") {
  Rig rig;
  rig.input.quit_after_ticks = 3;
  CHECK(rig.run() == 0);
  CHECK_FALSE(rig.controls.model().live);
  CHECK(rig.surfaces.carplay_active());
}

// ---------------------------------------------------------------------------
// touch
// ---------------------------------------------------------------------------

TEST_CASE("run_render_loop: a tap on the Settings tab opens the settings surface", "[render_loop]") {
  Rig rig;
  rig.input.quit_after_ticks = 4;
  rig.input.tap_at(2, rig.tab(Tab::Settings));
  CHECK(rig.run() == 0);
  CHECK(rig.surfaces.page() == Page::Root);
  CHECK_FALSE(rig.surfaces.carplay_active());
}

TEST_CASE("run_render_loop: a tap on the dock becomes a command once the relays are live", "[render_loop]") {
  Rig rig;
  rig.input.quit_after_ticks = 5;
  rig.source.frames.push_back(live_frame());  // adopted on tick 1
  rig.input.tap_at(3, rig.dock(DockControl::Blower3));
  CHECK(rig.run() == 0);
  REQUIRE(rig.sink.hvac.size() == 1);
  CHECK(rig.sink.hvac[0].blower == HvacBlower::L3);
}

TEST_CASE("run_render_loop: a dock tap before any frame is unavailable and sends nothing", "[render_loop]") {
  Rig rig;
  rig.input.quit_after_ticks = 4;
  rig.input.tap_at(2, rig.dock(DockControl::Blower3));
  CHECK(rig.run() == 0);
  CHECK(rig.sink.hvac.empty());
  CHECK(rig.controls.model().command == c3::vehicle::CommandStatus::Unavailable);
}

TEST_CASE("run_render_loop: a touch inside the well while CarPlay owns it reaches no local control", "[render_loop]") {
  Rig rig;
  rig.input.quit_after_ticks = 4;
  rig.source.frames.push_back(live_frame());
  rig.input.tap_at(2, rig.layout.well);
  CHECK(rig.run() == 0);
  CHECK(rig.surfaces.page() == Page::None);
  CHECK(rig.sink.hvac.empty());
  CHECK(rig.sink.lamps.empty());
}

TEST_CASE("run_render_loop: a cancelled gesture activates nothing", "[render_loop]") {
  Rig rig;
  rig.input.quit_after_ticks = 4;
  rig.source.frames.push_back(live_frame());
  const Rect r = rig.dock(DockControl::Blower3);
  rig.input.script[2].push_back(TouchEvent{.phase = TouchPhase::Down, .id = 1, .x = r.cx(), .y = r.cy()});
  rig.input.script[2].push_back(TouchEvent{.phase = TouchPhase::Cancel, .id = 1, .x = r.cx(), .y = r.cy()});
  CHECK(rig.run() == 0);
  CHECK(rig.sink.hvac.empty());
  CHECK_FALSE(rig.controls.model().pressed.has_value());
}

TEST_CASE("run_render_loop: a committed settings change is written to the settings file", "[render_loop]") {
  Rig rig;
  const ScratchDir dir;
  rig.opts.settings_path = dir.path_of("settings.toml");
  rig.input.quit_after_ticks = 4;
  rig.input.tap_at(2, rig.theme_tool());  // dark -> light, an explicit preference
  CHECK(rig.run() == 0);
  CHECK(rig.surfaces.settings().theme == c3::render::ThemePreference::Light);
  const auto loaded = c3::app::load_settings(rig.opts.settings_path, c3::render::Settings{});
  CHECK(loaded.status == c3::app::SettingsStatus::Loaded);
  CHECK(loaded.settings.theme == c3::render::ThemePreference::Light);
}

TEST_CASE("run_render_loop: without a settings path a settings change is kept in memory only", "[render_loop]") {
  Rig rig;
  rig.input.quit_after_ticks = 4;
  rig.input.tap_at(2, rig.theme_tool());
  CHECK(rig.run() == 0);
  CHECK(rig.surfaces.settings().theme == c3::render::ThemePreference::Light);
}

}  // namespace
