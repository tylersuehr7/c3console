#include "well_controller.hpp"

#include "logging/log.hpp"
#include "render/widgets.hpp"
#include "utils/fixed_format.hpp"

#include <algorithm>
#include <cmath>

namespace c3::app {
namespace {

constexpr std::string_view kUnit = "ui";

using render::Page;
using render::SheetControl;
using render::Tab;
using render::Tool;

// `frac` in 0..1 mapped onto lo..hi, rounded to the nearest step.
[[nodiscard]] int scaled_int(float frac, int lo, int hi) noexcept {
  return lo + static_cast<int>(std::lround(frac * static_cast<float>(hi - lo)));
}

}  // namespace

void LogAudioSink::apply(const audio::AudioState& s) noexcept {
  logging::info("audio", "\xE2\x86\x92 dsp vol=", s.volume, s.mute ? " muted" : "", " bal=", s.balance,
                " fade=", s.fader, " sub=", s.sub_db, " bass=", s.bass_db, " mid=", s.mid_db, " treble=", s.treble_db,
                " preset=", to_string(s.preset));
}

WellController::WellController(const render::Layout& layout, render::Settings initial,
                               const telemetry::CalibrationTable* calibration, audio::AudioSink& audio) noexcept
    : layout_(layout),
      tabs_(render::TabsLayout::compute(layout.tabs)),
      tools_(render::ToolsLayout::compute(layout.status)),
      sheets_(render::compute_pages(layout)),
      camera_(render::CameraLayout::compute(layout.well)),
      calibration_(calibration),
      audio_sink_(audio),
      settings_(initial) {}

void WellController::apply_telemetry(const telemetry::Frame& frame, bool live, std::uint64_t now_ms) noexcept {
  now_ms_ = now_ms;

  if (!live) {
    return;  // hold the last gear and lamp state, like HVAC (invariant 6)
  }

  if (frame.relays && frame.relays->headlights_on != headlights_) {
    headlights_ = frame.relays->headlights_on;
    if (settings_.theme == render::ThemePreference::Auto) {
      theme_changed_ = true;
    }
  }

  if (frame.reverse != reverse_) {
    if (!frame.reverse) {
      hold_until_ms_ = now_ms + kCameraHoldMs;
    }
    reverse_ = frame.reverse;
    if (reverse_) {
      manual_camera_ = false;  // the car has the well now; the settings page stays underneath
    }
  }
}

bool WellController::open_surface(std::string_view name) noexcept {
  if (name == "camera") {
    manual_camera_ = true;
    return true;
  }

  constexpr std::array<std::pair<std::string_view, Page>, 6> kPages = {{
      {"settings", Page::Root},
      {"audio", Page::Audio},
      {"theme", Page::Theme},
      {"units", Page::Units},
      {"calibration", Page::Calibration},
      {"system", Page::System},
  }};

  const auto* const found =
      std::ranges::find_if(kPages, [name](const auto& entry) noexcept { return entry.first == name; });

  if (found == kPages.end()) {
    return false;
  }

  show_page(found->second);
  return true;
}

bool WellController::camera_showing() const noexcept {
  return car_owns_well() || manual_camera_;
}

render::CameraState WellController::camera_state() const noexcept {
  if (reverse_) {
    return render::CameraState::Reverse;
  }

  if (now_ms_ < hold_until_ms_) {
    return render::CameraState::Hold;
  }

  return render::CameraState::Preview;
}

std::optional<render::Theme> WellController::take_theme_change() noexcept {
  if (!theme_changed_) {
    return std::nullopt;
  }

  theme_changed_ = false;
  return theme();
}

bool WellController::on_touch(const platform::TouchEvent& ev) noexcept {
  switch (ev.phase) {
    case platform::TouchPhase::Down:
      press_down(ev);
      return press_ != Press::None;
    case platform::TouchPhase::Move:
      if (press_ == Press::SheetControl && pressed_control_ && render::is_slider(*pressed_control_)) {
        set_slider(*pressed_control_, ev.x);
      }
      return press_ != Press::None;
    case platform::TouchPhase::Cancel:
      if (press_ == Press::SheetControl && pressed_control_ && render::is_slider(*pressed_control_)) {
        settings_ = settings_before_drag_;
        audio_ = audio_before_drag_;
        theme_changed_ = true;
      }
      press_ = Press::None;
      pressed_tab_.reset();
      pressed_tool_.reset();
      pressed_control_.reset();
      pressed_camera_.reset();
      return true;
    case platform::TouchPhase::Up: {
      const bool consumed = press_ != Press::None;
      release(ev);
      press_ = Press::None;
      pressed_tab_.reset();
      pressed_tool_.reset();
      pressed_control_.reset();
      pressed_camera_.reset();
      return consumed;
    }
  }
  return false;
}

void WellController::press_down(const platform::TouchEvent& ev) noexcept {
  press_ = Press::None;
  if (const auto tab = tabs_.hit_test(ev.x, ev.y)) {
    press_ = Press::Tab;
    pressed_tab_ = tab;
    return;
  }

  if (const auto tool = tools_.hit_test(ev.x, ev.y)) {
    press_ = Press::Tool;
    pressed_tool_ = tool;
    return;
  }

  if (!layout_.well.contains(ev.x, ev.y)) {
    return;  // the dock's
  }

  if (camera_showing()) {
    pressed_camera_ = camera_.hit_test(ev.x, ev.y, camera_state());
    press_ = pressed_camera_ ? Press::Camera : Press::CameraPicture;
    return;
  }

  if (page_ == Page::None) {
    return;  // CarPlay's; route_touch should not have sent it here
  }

  const render::SheetLayout& sl = sheet_layout();
  if (const auto control = sl.hit_test(ev.x, ev.y)) {
    press_ = Press::SheetControl;
    pressed_control_ = control;
    if (render::is_slider(*control)) {
      settings_before_drag_ = settings_;
      audio_before_drag_ = audio_;
      set_slider(*control, ev.x);
    }
    return;
  }

  press_ = Press::SheetPanel;
}

void WellController::release(const platform::TouchEvent& ev) noexcept {
  switch (press_) {
    case Press::Tab:
      if (pressed_tab_ && tabs_.hit_test(ev.x, ev.y) == pressed_tab_) {
        activate_tab(*pressed_tab_);
      }
      break;
    case Press::Tool:
      if (pressed_tool_ && tools_.hit_test(ev.x, ev.y) == pressed_tool_) {
        activate_tool(*pressed_tool_);
      }
      break;
    case Press::SheetControl:
      if (!pressed_control_) {
        break;
      }
      if (render::is_slider(*pressed_control_)) {
        // Sliders track the finger up to and including the release, which commits the value.
        set_slider(*pressed_control_, ev.x);
        if (*pressed_control_ == SheetControl::Hue) {
          settings_changed();
        } else {
          audio_changed();
        }
      } else if (sheet_layout().hit_test(ev.x, ev.y) == pressed_control_) {
        activate_control(*pressed_control_);
      }
      break;
    case Press::Camera:
      if (pressed_camera_ && camera_.hit_test(ev.x, ev.y, camera_state()) == pressed_camera_) {
        if (*pressed_camera_ == render::CameraControl::Guides) {
          guides_ = !guides_;
        } else {
          manual_camera_ = false;
        }
      }
      break;
    case Press::SheetPanel:
    case Press::CameraPicture:
    case Press::None:
      break;
  }
}

// Tabs choose the surface. Settings tapped while open returns to its root; CarPlay clears
// everything; Camera is a preview the car can override at any time.
void WellController::activate_tab(Tab tab) noexcept {
  switch (tab) {
    case Tab::CarPlay:
      page_ = Page::None;
      manual_camera_ = false;
      break;
    case Tab::Camera:
      if (car_owns_well()) {
        return;  // the car decides while reversing
      }
      manual_camera_ = true;
      page_ = Page::None;
      break;
    case Tab::Settings:
      show_page(Page::Root);
      break;
    case Tab::Count:
      break;
  }
  logging::info(kUnit, "tab ", render::to_string(tab));
}

void WellController::activate_tool(Tool tool) noexcept {
  switch (tool) {
    case Tool::Theme:
      // Explicit choice: leaves Auto.
      settings_.theme = dark() ? render::ThemePreference::Light : render::ThemePreference::Dark;
      settings_changed();
      break;
    case Tool::Count:
      break;
  }
}

void WellController::show_page(Page p) noexcept {
  if (p != Page::None) {
    manual_camera_ = false;
  }
  page_ = p;
}

void WellController::activate_control(SheetControl c) noexcept {
  if (const auto page = render::page_for(c)) {
    show_page(*page);
    return;
  }
  switch (c) {
    case SheetControl::Close:
      show_page(Page::None);  // × is always "back to CarPlay", whatever the depth
      return;
    case SheetControl::Back:
      show_page(Page::Root);
      return;
    case SheetControl::Mute:
      audio_.mute = !audio_.mute;
      audio_changed();
      return;
    case SheetControl::PresetFlat:
    case SheetControl::PresetRock:
    case SheetControl::PresetCruise:
    case SheetControl::PresetVoice:
      audio_ = audio::with_preset(
          audio_, static_cast<audio::Preset>(static_cast<int>(c) - static_cast<int>(SheetControl::PresetFlat)));
      audio_changed();
      return;
    case SheetControl::ThemeDark:
    case SheetControl::ThemeLight:
    case SheetControl::ThemeAuto:
      settings_.theme =
          static_cast<render::ThemePreference>(static_cast<int>(c) - static_cast<int>(SheetControl::ThemeDark));
      break;
    case SheetControl::Swatch0:
    case SheetControl::Swatch1:
    case SheetControl::Swatch2:
    case SheetControl::Swatch3:
    case SheetControl::Swatch4:
    case SheetControl::Swatch5:
    case SheetControl::Swatch6:
    case SheetControl::Swatch7:
    case SheetControl::Swatch8:
      settings_.accent =
          render::kSwatches[static_cast<std::size_t>(static_cast<int>(c) - static_cast<int>(SheetControl::Swatch0))]
              .rgb;
      break;
    case SheetControl::TempF:
      settings_.units.temp = render::TempUnit::Fahrenheit;
      break;
    case SheetControl::TempC:
      settings_.units.temp = render::TempUnit::Celsius;
      break;
    case SheetControl::PressPsi:
      settings_.units.pressure = render::PressureUnit::Psi;
      break;
    case SheetControl::PressBar:
      settings_.units.pressure = render::PressureUnit::Bar;
      break;
    case SheetControl::Reset:
      settings_ = render::Settings{};
      break;
    case SheetControl::RowAudio:
    case SheetControl::RowTheme:
    case SheetControl::RowUnits:
    case SheetControl::RowCalibration:
    case SheetControl::RowSystem:
    case SheetControl::Volume:
    case SheetControl::Balance:
    case SheetControl::Fader:
    case SheetControl::Sub:
    case SheetControl::Bass:
    case SheetControl::Mid:
    case SheetControl::Treble:
    case SheetControl::Hue:
    case SheetControl::Count:
      return;  // rows are handled above; sliders on move
  }
  settings_changed();
}

void WellController::set_slider(SheetControl c, float x) noexcept {
  const render::SheetLayout& sl = sheet_layout();
  const float frac = render::widgets::slider_fraction(sl.rect(c), sl.scale, render::slider_style(c), x);
  switch (c) {
    case SheetControl::Volume:
      audio_.volume = scaled_int(frac, 0, 100);
      if (audio_.volume > 0) {
        audio_.mute = false;  // touching the volume un-mutes, like every car
      }
      break;
    case SheetControl::Balance:
      audio_.balance = scaled_int(frac, -10, 10);
      break;
    case SheetControl::Fader:
      audio_.fader = scaled_int(frac, -10, 10);
      break;
    case SheetControl::Sub:
      audio_.sub_db = scaled_int(frac, -12, 6);
      break;
    case SheetControl::Bass:
      audio_.bass_db = scaled_int(frac, -6, 6);
      break;
    case SheetControl::Mid:
      audio_.mid_db = scaled_int(frac, -6, 6);
      break;
    case SheetControl::Treble:
      audio_.treble_db = scaled_int(frac, -6, 6);
      break;
    case SheetControl::Hue:
      settings_.accent = render::accent_from_hue(frac * 359.0F);  // 359 so the ends differ
      theme_changed_ = true;  // live preview while dragging; committed and logged on release
      break;
    default:
      break;
  }
}

std::optional<render::Settings> WellController::take_settings_change() noexcept {
  if (!settings_changed_) {
    return std::nullopt;
  }

  settings_changed_ = false;
  return settings_;
}

void WellController::settings_changed() noexcept {
  theme_changed_ = true;
  settings_changed_ = true;
  std::array<char, 8> hex{};
  logging::info(kUnit, "settings theme=", to_string(settings_.theme), " accent=#",
                utils::format_into(hex, "%06X", settings_.accent), " temp=", to_string(settings_.units.temp),
                " pressure=", to_string(settings_.units.pressure));
}

void WellController::audio_changed() noexcept {
  audio_sink_.apply(audio_);
}

render::WellView WellController::view(const telemetry::Frame* frame, bool live,
                                      telemetry::LinkState link) const noexcept {
  const bool camera = camera_showing();
  const Tab active = camera ? Tab::Camera : page_ != Page::None ? Tab::Settings : Tab::CarPlay;
  return {
      .tabs = {.active = active,
               .reverse = car_owns_well(),
               .pressed = press_ == Press::Tab ? pressed_tab_ : std::nullopt},
      .tools = {.dark = dark(), .pressed = press_ == Press::Tool ? pressed_tool_ : std::nullopt},
      .page = camera ? Page::None : page_,
      .sheet_model = {.audio = audio_,
                      .settings = settings_,
                      .pressed = press_ == Press::SheetControl ? pressed_control_ : std::nullopt,
                      .link = link,
                      .calibration = calibration_,
                      .frame = frame,
                      .live = live},
      .camera = camera,
      .camera_model = {.state = camera_state(),
                       .frames = false,
                       .guides = guides_,
                       .pressed = press_ == Press::Camera ? pressed_camera_ : std::nullopt},
  };
}

}  // namespace c3::app
