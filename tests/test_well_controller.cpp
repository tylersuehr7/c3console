// well_controller.cpp — what occupies the well: tabs, settings pages, camera, reverse, and the
// settings and audio state the surfaces edit.
#include "app/well_controller.hpp"
#include "audio/audio_state.hpp"
#include "logging/log.hpp"
#include "platform/input_source.hpp"
#include "render/camera_surface.hpp"
#include "render/console_scene.hpp"
#include "render/layout.hpp"
#include "render/settings.hpp"
#include "render/sheet.hpp"
#include "render/status_tools.hpp"
#include "render/tabs.hpp"
#include "render/theme.hpp"
#include "render/units.hpp"
#include "render/widgets.hpp"
#include "telemetry/calibration.hpp"
#include "telemetry/frame.hpp"
#include "telemetry/sensor_source.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

using c3::app::WellController;
using c3::audio::AudioState;
using c3::audio::Preset;
using c3::platform::Size;
using c3::platform::TouchEvent;
using c3::platform::TouchPhase;
using c3::render::CameraControl;
using c3::render::CameraState;
using c3::render::Layout;
using c3::render::Page;
using c3::render::PressureUnit;
using c3::render::Rect;
using c3::render::Settings;
using c3::render::SheetControl;
using c3::render::SheetLayout;
using c3::render::Tab;
using c3::render::TempUnit;
using c3::render::ThemePreference;
using c3::render::Tool;
using c3::render::WellView;
using c3::telemetry::Frame;
using c3::telemetry::LinkState;
using Catch::Matchers::WithinAbs;

CATCH_REGISTER_ENUM(Page, Page::None, Page::Root, Page::Audio, Page::Theme, Page::Units, Page::Calibration,
                    Page::System, Page::Count)
CATCH_REGISTER_ENUM(Tab, Tab::CarPlay, Tab::Camera, Tab::Settings, Tab::Count)
CATCH_REGISTER_ENUM(CameraState, CameraState::Preview, CameraState::Reverse, CameraState::Hold)
CATCH_REGISTER_ENUM(ThemePreference, ThemePreference::Dark, ThemePreference::Light, ThemePreference::Auto)
CATCH_REGISTER_ENUM(Preset, Preset::Flat, Preset::Rock, Preset::Cruise, Preset::Voice, Preset::Count)

namespace {

constexpr Size kReferencePanel{.width = 1600, .height = 2560};

/// Records every state the DSP stage would receive.
class RecordingAudioSink final : public c3::audio::AudioSink {
public:
  std::vector<AudioState> applied;
  void apply(const AudioState& s) noexcept override { applied.push_back(s); }
};

/// Discards log lines so tab and settings echoes stay out of the test output.
class QuietLog {
public:
  QuietLog() : sink_(c3::logging::Sink{.context = nullptr, .emit = &QuietLog::discard}) {}

private:
  static void discard(void*, std::string_view) noexcept {}
  c3::logging::ScopedSink sink_;
};

[[nodiscard]] Frame live_frame(bool reverse, bool headlights = false) noexcept {
  Frame f;
  f.reverse = reverse;
  f.relays = c3::vehicle::RelayState{.headlights_on = headlights};
  return f;
}

/// A controller over the reference layout with the initial theme change already consumed.
struct Rig {
  QuietLog quiet;
  Layout layout = Layout::compute(kReferencePanel);
  RecordingAudioSink audio;
  WellController ctl;

  explicit Rig(Settings initial = {}) : ctl(layout, initial, nullptr, audio) {
    static_cast<void>(ctl.take_theme_change());
  }

  [[nodiscard]] bool touch(TouchPhase phase, float x, float y) {
    return ctl.on_touch(TouchEvent{.phase = phase, .id = 1, .x = x, .y = y});
  }
  [[nodiscard]] bool touch(TouchPhase phase, const Rect& r) { return touch(phase, r.cx(), r.cy()); }
  void tap(const Rect& r) {
    static_cast<void>(touch(TouchPhase::Down, r));
    static_cast<void>(touch(TouchPhase::Up, r));
  }
  void tap_tab(Tab t) { tap(c3::render::TabsLayout::compute(layout.tabs).rect(t)); }
  void tap_tool() { tap(c3::render::ToolsLayout::compute(layout.status).rect(Tool::Theme)); }

  [[nodiscard]] SheetLayout sheet(Page p) const { return SheetLayout::compute(p, layout.well, layout.panel); }
  void tap_control(SheetControl c) { tap(sheet(ctl.page()).rect(c)); }

  [[nodiscard]] WellView view() const { return ctl.view(nullptr, false, LinkState::NoData); }

  /// The x at which a slider reads `frac`.
  [[nodiscard]] float slider_x(SheetControl c, float frac) const {
    const SheetLayout sl = sheet(ctl.page());
    const Rect r = sl.rect(c);
    const float half = c3::render::slider_style(c).thumb_d * sl.scale / 2.0F;
    return r.x + half + (r.w - 2.0F * half) * frac;
  }
};

// ---------------------------------------------------------------------------
// initial state
// ---------------------------------------------------------------------------

TEST_CASE("WellController: starts on CarPlay with the initial settings and one pending theme", "[well_controller]") {
  QuietLog quiet;
  Layout layout = Layout::compute(kReferencePanel);
  RecordingAudioSink audio;
  const Settings initial{.theme = ThemePreference::Light, .accent = 0x2DD4BF};
  WellController ctl(layout, initial, nullptr, audio);

  CHECK(ctl.carplay_active());
  CHECK_FALSE(ctl.camera_showing());
  CHECK(ctl.page() == Page::None);
  CHECK(ctl.settings() == initial);
  CHECK(ctl.audio() == AudioState{});
  CHECK_FALSE(ctl.dark());

  const auto theme = ctl.take_theme_change();
  REQUIRE(theme.has_value());
  CHECK(theme->bg.r == c3::render::Theme::light().bg.r);
  CHECK(theme->ember.g == c3::render::rgb(0x2DD4BF).g);
  CHECK_FALSE(ctl.take_theme_change().has_value());  // delivered once
  CHECK_FALSE(ctl.take_settings_change().has_value());
  CHECK(audio.applied.empty());

  const WellView v = ctl.view(nullptr, false, LinkState::NoData);
  CHECK(v.tabs.active == Tab::CarPlay);
  CHECK_FALSE(v.tabs.reverse);
  CHECK(v.page == Page::None);
  CHECK_FALSE(v.camera);
  CHECK_FALSE(v.tools.dark);
}

TEST_CASE("WellController: view carries the frame, link and calibration for the sheet", "[well_controller]") {
  QuietLog quiet;
  const Layout layout = Layout::compute(kReferencePanel);
  RecordingAudioSink audio;
  c3::telemetry::CalibrationTable table;
  WellController ctl(layout, Settings{}, &table, audio);
  const Frame frame;
  const WellView v = ctl.view(&frame, true, LinkState::Live);
  CHECK(v.sheet_model.frame == &frame);
  CHECK(v.sheet_model.live);
  CHECK(v.sheet_model.link == LinkState::Live);
  CHECK(v.sheet_model.calibration == &table);
  CHECK(v.sheet_model.settings == Settings{});
}

// ---------------------------------------------------------------------------
// tabs and the theme button
// ---------------------------------------------------------------------------

TEST_CASE("WellController: the tabs choose the surface", "[well_controller]") {
  Rig rig;
  rig.tap_tab(Tab::Settings);
  CHECK(rig.ctl.page() == Page::Root);
  CHECK_FALSE(rig.ctl.carplay_active());
  CHECK(rig.view().tabs.active == Tab::Settings);
  CHECK(rig.view().page == Page::Root);

  rig.tap_tab(Tab::Camera);
  CHECK(rig.ctl.camera_showing());
  CHECK(rig.ctl.page() == Page::None);
  CHECK(rig.view().tabs.active == Tab::Camera);
  CHECK(rig.view().camera);
  CHECK(rig.view().camera_model.state == CameraState::Preview);
  CHECK_FALSE(rig.view().tabs.reverse);  // a preview is not the car's doing

  rig.tap_tab(Tab::CarPlay);
  CHECK(rig.ctl.carplay_active());
  CHECK_FALSE(rig.ctl.camera_showing());
  CHECK(rig.view().tabs.active == Tab::CarPlay);
}

TEST_CASE("WellController: Settings tapped while open returns to the root; a page tab clears the camera",
          "[well_controller]") {
  Rig rig;
  rig.tap_tab(Tab::Settings);
  rig.tap_control(SheetControl::RowUnits);
  CHECK(rig.ctl.page() == Page::Units);
  rig.tap_tab(Tab::Settings);
  CHECK(rig.ctl.page() == Page::Root);

  rig.tap_tab(Tab::Camera);
  CHECK(rig.ctl.camera_showing());
  rig.tap_tab(Tab::Settings);
  CHECK_FALSE(rig.ctl.camera_showing());
  CHECK(rig.ctl.page() == Page::Root);
}

TEST_CASE("WellController: a tab press shows feedback and activates only when released on the same tab",
          "[well_controller]") {
  Rig rig;
  const auto tabs = c3::render::TabsLayout::compute(rig.layout.tabs);
  CHECK(rig.touch(TouchPhase::Down, tabs.rect(Tab::Settings)));
  CHECK(rig.view().tabs.pressed == Tab::Settings);
  CHECK(rig.touch(TouchPhase::Up, tabs.rect(Tab::Camera)));  // slid off
  CHECK(rig.ctl.page() == Page::None);
  CHECK_FALSE(rig.ctl.camera_showing());
  CHECK_FALSE(rig.view().tabs.pressed.has_value());
}

TEST_CASE("WellController: the theme button sets an explicit dark or light preference", "[well_controller]") {
  Rig rig(Settings{.theme = ThemePreference::Auto});  // Auto with the lamps off is light
  CHECK_FALSE(rig.ctl.dark());
  rig.tap_tool();
  CHECK(rig.ctl.settings().theme == ThemePreference::Dark);
  CHECK(rig.ctl.dark());
  CHECK(rig.ctl.take_settings_change() == Settings{.theme = ThemePreference::Dark});
  CHECK(rig.ctl.take_theme_change().has_value());
  rig.tap_tool();
  CHECK(rig.ctl.settings().theme == ThemePreference::Light);
  CHECK(rig.view().tools.dark == false);
}

// ---------------------------------------------------------------------------
// settings pages
// ---------------------------------------------------------------------------

TEST_CASE("WellController: root rows open their pages, Back returns to the root and Close to CarPlay",
          "[well_controller]") {
  Rig rig;
  rig.tap_tab(Tab::Settings);
  struct Row {
    SheetControl row;
    Page page;
  };
  for (const Row r : {Row{SheetControl::RowAudio, Page::Audio}, Row{SheetControl::RowTheme, Page::Theme},
                      Row{SheetControl::RowUnits, Page::Units}, Row{SheetControl::RowCalibration, Page::Calibration},
                      Row{SheetControl::RowSystem, Page::System}}) {
    rig.tap_control(r.row);
    CHECK(rig.ctl.page() == r.page);
    rig.tap_control(SheetControl::Back);
    CHECK(rig.ctl.page() == Page::Root);
  }
  rig.tap_control(SheetControl::RowSystem);
  rig.tap_control(SheetControl::Close);
  CHECK(rig.ctl.page() == Page::None);
  CHECK(rig.ctl.carplay_active());
  CHECK_FALSE(rig.ctl.take_settings_change().has_value());  // navigation is not a settings change
}

TEST_CASE("WellController: a touch on the sheet panel is consumed and does nothing", "[well_controller]") {
  Rig rig;
  rig.tap_tab(Tab::Settings);
  const Rect& well = rig.layout.well;
  CHECK(rig.touch(TouchPhase::Down, well.cx(), well.y + 60.0F));  // the title, between back and close
  CHECK(rig.touch(TouchPhase::Up, well.cx(), well.y + 60.0F));
  CHECK(rig.ctl.page() == Page::Root);
}

TEST_CASE("WellController: a touch outside the surfaces is the dock's", "[well_controller]") {
  Rig rig;
  CHECK_FALSE(rig.touch(TouchPhase::Down, rig.layout.dock));
  CHECK_FALSE(rig.touch(TouchPhase::Up, rig.layout.dock));
  // and a touch in the well while CarPlay owns it is not ours either
  CHECK_FALSE(rig.touch(TouchPhase::Down, rig.layout.well));
  CHECK_FALSE(rig.touch(TouchPhase::Up, rig.layout.well));
}

TEST_CASE("WellController: unit, theme and swatch buttons commit a settings change", "[well_controller]") {
  Rig rig;
  rig.tap_tab(Tab::Settings);
  rig.tap_control(SheetControl::RowUnits);
  rig.tap_control(SheetControl::TempC);
  CHECK(rig.ctl.settings().units.temp == TempUnit::Celsius);
  const auto change = rig.ctl.take_settings_change();
  REQUIRE(change.has_value());
  CHECK(change->units.temp == TempUnit::Celsius);
  CHECK(rig.ctl.take_theme_change().has_value());  // the scene is told even if only units changed
  rig.tap_control(SheetControl::PressBar);
  CHECK(rig.ctl.settings().units.pressure == PressureUnit::Bar);

  rig.tap_control(SheetControl::Back);
  rig.tap_control(SheetControl::RowTheme);
  rig.tap_control(SheetControl::ThemeLight);
  CHECK(rig.ctl.settings().theme == ThemePreference::Light);
  CHECK_FALSE(rig.ctl.dark());
  rig.tap_control(SheetControl::ThemeAuto);
  CHECK(rig.ctl.settings().theme == ThemePreference::Auto);
  rig.tap_control(SheetControl::Swatch3);
  CHECK(rig.ctl.settings().accent == c3::render::kSwatches[3].rgb);
  const auto theme = rig.ctl.take_theme_change();
  REQUIRE(theme.has_value());
  CHECK(theme->ember.r == c3::render::rgb(c3::render::kSwatches[3].rgb).r);
}

TEST_CASE("WellController: Reset restores the default settings", "[well_controller]") {
  Rig rig(Settings{.theme = ThemePreference::Light, .accent = 0x123456});
  rig.tap_tab(Tab::Settings);
  rig.tap_control(SheetControl::RowSystem);
  rig.tap_control(SheetControl::Reset);
  CHECK(rig.ctl.settings() == Settings{});
  CHECK(rig.ctl.take_settings_change() == Settings{});
}

TEST_CASE("WellController: a control press activates only when released over the same control", "[well_controller]") {
  Rig rig;
  rig.tap_tab(Tab::Settings);
  const SheetLayout root = rig.sheet(Page::Root);
  CHECK(rig.touch(TouchPhase::Down, root.rect(SheetControl::RowAudio)));
  CHECK(rig.view().sheet_model.pressed == SheetControl::RowAudio);
  CHECK(rig.touch(TouchPhase::Up, root.rect(SheetControl::RowTheme)));
  CHECK(rig.ctl.page() == Page::Root);
  CHECK_FALSE(rig.view().sheet_model.pressed.has_value());
}

// ---------------------------------------------------------------------------
// audio
// ---------------------------------------------------------------------------

TEST_CASE("WellController: mute and presets apply to the audio sink at once", "[well_controller]") {
  Rig rig;
  rig.tap_tab(Tab::Settings);
  rig.tap_control(SheetControl::RowAudio);
  rig.tap_control(SheetControl::Mute);
  REQUIRE(rig.audio.applied.size() == 1);
  CHECK(rig.audio.applied[0].mute);
  CHECK(rig.ctl.audio().mute);
  rig.tap_control(SheetControl::Mute);
  REQUIRE(rig.audio.applied.size() == 2);
  CHECK_FALSE(rig.audio.applied[1].mute);

  rig.tap_control(SheetControl::PresetRock);
  REQUIRE(rig.audio.applied.size() == 3);
  CHECK(rig.audio.applied[2] == c3::audio::with_preset(rig.audio.applied[1], Preset::Rock));
  CHECK(rig.ctl.audio().preset == Preset::Rock);
  rig.tap_control(SheetControl::PresetVoice);
  CHECK(rig.ctl.audio().preset == Preset::Voice);
  CHECK(rig.ctl.audio().mid_db == 3);
  CHECK_FALSE(rig.ctl.take_settings_change().has_value());  // audio is not a persisted setting
}

TEST_CASE("WellController: a slider follows the finger and commits once on release", "[well_controller]") {
  Rig rig;
  rig.tap_tab(Tab::Settings);
  rig.tap_control(SheetControl::RowAudio);
  const Rect volume = rig.sheet(Page::Audio).rect(SheetControl::Volume);

  CHECK(rig.touch(TouchPhase::Down, rig.slider_x(SheetControl::Volume, 0.25F), volume.cy()));
  CHECK(rig.ctl.audio().volume == 25);
  CHECK(rig.audio.applied.empty());                                                                    // not yet
  CHECK(rig.touch(TouchPhase::Move, rig.slider_x(SheetControl::Volume, 0.8F), volume.cy() + 500.0F));  // y is free
  CHECK(rig.ctl.audio().volume == 80);
  CHECK(rig.touch(TouchPhase::Move, rig.slider_x(SheetControl::Volume, 1.0F) + 400.0F, volume.cy()));  // clamps
  CHECK(rig.ctl.audio().volume == 100);
  CHECK(rig.touch(TouchPhase::Up, rig.slider_x(SheetControl::Volume, 0.5F), volume.cy()));
  CHECK(rig.ctl.audio().volume == 50);
  REQUIRE(rig.audio.applied.size() == 1);
  CHECK(rig.audio.applied[0].volume == 50);
}

TEST_CASE("WellController: the slider's label row is part of the target and the ends read the extremes",
          "[well_controller]") {
  Rig rig;
  rig.tap_tab(Tab::Settings);
  rig.tap_control(SheetControl::RowAudio);
  const Rect balance = rig.sheet(Page::Audio).rect(SheetControl::Balance);
  CHECK(rig.touch(TouchPhase::Down, rig.slider_x(SheetControl::Balance, 0.0F), balance.y - 10.0F));
  CHECK(rig.ctl.audio().balance == -10);
  CHECK(rig.touch(TouchPhase::Up, rig.slider_x(SheetControl::Balance, 1.0F), balance.y - 10.0F));
  CHECK(rig.ctl.audio().balance == 10);

  const Rect sub = rig.sheet(Page::Audio).rect(SheetControl::Sub);
  CHECK(rig.touch(TouchPhase::Down, rig.slider_x(SheetControl::Sub, 0.0F), sub.cy()));
  CHECK(rig.ctl.audio().sub_db == -12);
  CHECK(rig.touch(TouchPhase::Up, rig.slider_x(SheetControl::Sub, 1.0F), sub.cy()));
  CHECK(rig.ctl.audio().sub_db == 6);
  CHECK(rig.audio.applied.size() == 2);
}

TEST_CASE("WellController: touching the volume un-mutes", "[well_controller]") {
  Rig rig;
  rig.tap_tab(Tab::Settings);
  rig.tap_control(SheetControl::RowAudio);
  rig.tap_control(SheetControl::Mute);
  REQUIRE(rig.ctl.audio().mute);
  const Rect volume = rig.sheet(Page::Audio).rect(SheetControl::Volume);
  CHECK(rig.touch(TouchPhase::Down, rig.slider_x(SheetControl::Volume, 0.5F), volume.cy()));
  CHECK_FALSE(rig.ctl.audio().mute);
  CHECK(rig.touch(TouchPhase::Up, rig.slider_x(SheetControl::Volume, 0.5F), volume.cy()));
  CHECK(rig.audio.applied.back().volume == 50);
  CHECK_FALSE(rig.audio.applied.back().mute);
}

TEST_CASE("WellController: Cancel during a drag rolls the preview back and applies nothing", "[well_controller]") {
  Rig rig;
  rig.tap_tab(Tab::Settings);
  rig.tap_control(SheetControl::RowAudio);
  const Rect volume = rig.sheet(Page::Audio).rect(SheetControl::Volume);
  const AudioState before = rig.ctl.audio();
  CHECK(rig.touch(TouchPhase::Down, rig.slider_x(SheetControl::Volume, 0.9F), volume.cy()));
  CHECK(rig.ctl.audio().volume == 90);
  CHECK(rig.touch(TouchPhase::Cancel, rig.slider_x(SheetControl::Volume, 0.9F), volume.cy()));
  CHECK(rig.ctl.audio() == before);
  CHECK(rig.audio.applied.empty());
  CHECK_FALSE(rig.view().sheet_model.pressed.has_value());
}

// ---------------------------------------------------------------------------
// hue
// ---------------------------------------------------------------------------

TEST_CASE("WellController: the hue slider previews live, commits on release and rolls back on cancel",
          "[well_controller]") {
  Rig rig;
  rig.tap_tab(Tab::Settings);
  rig.tap_control(SheetControl::RowTheme);
  const Rect hue = rig.sheet(Page::Theme).rect(SheetControl::Hue);
  const std::uint32_t before = rig.ctl.settings().accent;

  CHECK(rig.touch(TouchPhase::Down, rig.slider_x(SheetControl::Hue, 1.0F / 3.0F), hue.cy()));
  const std::uint32_t preview = rig.ctl.settings().accent;
  CHECK(preview != before);
  CHECK_THAT(c3::render::hue_of(preview), WithinAbs(359.0 / 3.0, 1.5));
  CHECK(rig.ctl.take_theme_change().has_value());           // the scene follows the drag
  CHECK_FALSE(rig.ctl.take_settings_change().has_value());  // nothing persisted yet
  CHECK(rig.touch(TouchPhase::Up, rig.slider_x(SheetControl::Hue, 2.0F / 3.0F), hue.cy()));
  const auto committed = rig.ctl.take_settings_change();
  REQUIRE(committed.has_value());
  CHECK_THAT(c3::render::hue_of(committed->accent), WithinAbs(2.0 * 359.0 / 3.0, 1.5));
  CHECK(rig.ctl.settings().accent == committed->accent);

  CHECK(rig.touch(TouchPhase::Down, rig.slider_x(SheetControl::Hue, 0.0F), hue.cy()));
  CHECK(rig.ctl.settings().accent != committed->accent);
  CHECK(rig.touch(TouchPhase::Cancel, rig.slider_x(SheetControl::Hue, 0.0F), hue.cy()));
  CHECK(rig.ctl.settings().accent == committed->accent);
  CHECK(rig.ctl.take_theme_change().has_value());  // the rollback is drawn too
  CHECK_FALSE(rig.ctl.take_settings_change().has_value());
}

// ---------------------------------------------------------------------------
// reverse and the camera
// ---------------------------------------------------------------------------

TEST_CASE("WellController: reverse in a live frame takes the well and holds it for two seconds after",
          "[well_controller]") {
  Rig rig;
  rig.tap_tab(Tab::Settings);
  rig.ctl.apply_telemetry(live_frame(true), true, 1000);
  CHECK(rig.ctl.camera_showing());
  CHECK_FALSE(rig.ctl.carplay_active());
  WellView v = rig.view();
  CHECK(v.camera);
  CHECK(v.camera_model.state == CameraState::Reverse);
  CHECK(v.tabs.active == Tab::Camera);
  CHECK(v.tabs.reverse);
  CHECK(v.page == Page::None);          // the sheet is covered
  CHECK(rig.ctl.page() == Page::Root);  // but not forgotten

  rig.ctl.apply_telemetry(live_frame(false), true, 5000);
  CHECK(rig.ctl.camera_showing());
  CHECK(rig.view().camera_model.state == CameraState::Hold);
  CHECK(rig.view().tabs.reverse);
  rig.ctl.tick(5000 + WellController::kCameraHoldMs - 1);
  CHECK(rig.ctl.camera_showing());
  rig.ctl.tick(5000 + WellController::kCameraHoldMs);
  CHECK_FALSE(rig.ctl.camera_showing());
  CHECK(rig.view().page == Page::Root);  // the settings page comes back
  CHECK(rig.view().tabs.active == Tab::Settings);
}

TEST_CASE("WellController: reverse is trusted only from a live frame; a stall holds the last gear",
          "[well_controller]") {
  Rig rig;
  rig.ctl.apply_telemetry(live_frame(true), false, 1000);
  CHECK_FALSE(rig.ctl.camera_showing());
  rig.ctl.apply_telemetry(live_frame(true), true, 2000);
  CHECK(rig.ctl.camera_showing());
  rig.ctl.apply_telemetry(live_frame(false), false, 3000);  // stale: ignored
  CHECK(rig.ctl.camera_showing());
  CHECK(rig.view().camera_model.state == CameraState::Reverse);
}

TEST_CASE("WellController: the Camera tab is ignored while the car owns the well, and reverse ends a preview",
          "[well_controller]") {
  Rig rig;
  rig.tap_tab(Tab::Camera);
  CHECK(rig.ctl.camera_showing());
  rig.ctl.apply_telemetry(live_frame(true), true, 1000);
  rig.ctl.apply_telemetry(live_frame(false), true, 2000);
  rig.ctl.tick(2000 + WellController::kCameraHoldMs);
  CHECK_FALSE(rig.ctl.camera_showing());  // the preview did not survive the car's turn
  CHECK(rig.ctl.carplay_active());

  rig.ctl.apply_telemetry(live_frame(true), true, 3000);
  rig.tap_tab(Tab::Camera);
  rig.tap_tab(Tab::CarPlay);  // CarPlay clears the manual flag but cannot clear reverse
  CHECK(rig.ctl.camera_showing());
  CHECK(rig.view().camera_model.state == CameraState::Reverse);
}

TEST_CASE("WellController: camera tools toggle the guides and close a preview; the picture swallows touches",
          "[well_controller]") {
  Rig rig;
  rig.tap_tab(Tab::Camera);
  const auto cam = c3::render::CameraLayout::compute(rig.layout.well);
  CHECK(rig.view().camera_model.guides);
  rig.tap(cam.guides_button);
  CHECK_FALSE(rig.view().camera_model.guides);
  CHECK(rig.touch(TouchPhase::Down, cam.guides_button));
  CHECK(rig.view().camera_model.pressed == CameraControl::Guides);
  CHECK(rig.touch(TouchPhase::Up, cam.guides_button));
  CHECK(rig.view().camera_model.guides);

  CHECK(rig.touch(TouchPhase::Down, rig.layout.well));  // the picture
  CHECK(rig.touch(TouchPhase::Up, rig.layout.well));
  CHECK(rig.ctl.camera_showing());

  rig.tap(cam.close_button);
  CHECK_FALSE(rig.ctl.camera_showing());
  CHECK(rig.ctl.carplay_active());
}

TEST_CASE("WellController: the close tool does not exist while reversing", "[well_controller]") {
  Rig rig;
  rig.ctl.apply_telemetry(live_frame(true), true, 1000);
  const auto cam = c3::render::CameraLayout::compute(rig.layout.well);
  CHECK(rig.touch(TouchPhase::Down, cam.close_button));  // consumed as the picture
  CHECK_FALSE(rig.view().camera_model.pressed.has_value());
  CHECK(rig.touch(TouchPhase::Up, cam.close_button));
  CHECK(rig.ctl.camera_showing());
}

// ---------------------------------------------------------------------------
// headlamps and the Auto theme
// ---------------------------------------------------------------------------

TEST_CASE("WellController: the headlamp relay drives the Auto theme and nothing else", "[well_controller]") {
  Rig night(Settings{.theme = ThemePreference::Auto});
  CHECK_FALSE(night.ctl.dark());
  night.ctl.apply_telemetry(live_frame(false, true), true, 1000);
  CHECK(night.ctl.dark());
  CHECK(night.ctl.take_theme_change().has_value());
  night.ctl.apply_telemetry(live_frame(false, true), true, 1100);  // unchanged: no new theme
  CHECK_FALSE(night.ctl.take_theme_change().has_value());
  CHECK_FALSE(night.ctl.take_settings_change().has_value());  // the lamps are not a preference

  Rig fixed(Settings{.theme = ThemePreference::Dark});
  fixed.ctl.apply_telemetry(live_frame(false, true), true, 1000);
  CHECK(fixed.ctl.dark());
  CHECK_FALSE(fixed.ctl.take_theme_change().has_value());  // the lamps did not change the picture
}

TEST_CASE("WellController: the theme button leaves Auto, taking the opposite of what is showing", "[well_controller]") {
  Rig rig(Settings{.theme = ThemePreference::Auto});
  rig.ctl.apply_telemetry(live_frame(false, true), true, 1000);  // lamps on: Auto shows dark
  REQUIRE(rig.ctl.dark());
  rig.tap_tool();
  CHECK(rig.ctl.settings().theme == ThemePreference::Light);
  CHECK_FALSE(rig.ctl.dark());
}

// ---------------------------------------------------------------------------
// open_surface
// ---------------------------------------------------------------------------

TEST_CASE("WellController: open_surface shows a surface by name and rejects unknown names", "[well_controller]") {
  Rig rig;
  struct Case {
    std::string_view name;
    Page page;
  };
  for (const Case c :
       {Case{"settings", Page::Root}, Case{"audio", Page::Audio}, Case{"theme", Page::Theme},
        Case{"units", Page::Units}, Case{"calibration", Page::Calibration}, Case{"system", Page::System}}) {
    INFO(c.name);
    CHECK(rig.ctl.open_surface(c.name));
    CHECK(rig.ctl.page() == c.page);
    CHECK_FALSE(rig.ctl.camera_showing());
  }
  CHECK(rig.ctl.open_surface("camera"));
  CHECK(rig.ctl.camera_showing());
  CHECK(rig.ctl.page() == Page::System);  // kept underneath
  CHECK_FALSE(rig.ctl.open_surface("carplay"));
  CHECK_FALSE(rig.ctl.open_surface("Settings"));
  CHECK(rig.ctl.camera_showing());  // an unknown name changes nothing
}

}  // namespace
