// WellController — what occupies the well, driven by the tab bar under it and the settings
// pages inside it (docs/c3-console-ui-design.md, "The well is a surface slot"; mockup v6).
//
// Priority every tick: rear camera (reverse in a live frame, 2 s hold after it clears, or the
// Camera tab) > the settings surface (root list or one nested page) > CarPlay. Touches are
// forwarded to the dongle only while CarPlay is active; everything else is consumed here.
// Reverse is trusted only from a live frame; on a stall the last gear state is kept, the same
// way HVAC holds state. Committed preferences are published to the background persistence worker.
#pragma once

#include "audio/audio_state.hpp"
#include "platform/input_source.hpp"
#include "render/console_scene.hpp"
#include "render/layout.hpp"
#include "render/settings.hpp"
#include "render/sheet.hpp"
#include "render/status_tools.hpp"
#include "render/tabs.hpp"
#include "render/theme.hpp"
#include "telemetry/calibration.hpp"
#include "telemetry/frame.hpp"
#include "telemetry/sensor_source.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace c3::app {

/// Desktop AudioSink: logs the state the DSP stage will receive.
class LogAudioSink final : public audio::AudioSink {
public:
  void apply(const audio::AudioState& s) noexcept override;
};

class WellController {
public:
  static constexpr std::uint32_t kCameraHoldMs = 2000;

  WellController(const render::Layout& layout, render::Settings initial, const telemetry::CalibrationTable* calibration,
                 audio::AudioSink& audio) noexcept;

  /// Reverse and headlamp state from the latest frame; `live` gates the reverse flag.
  void apply_telemetry(const telemetry::Frame& frame, bool live, std::uint64_t now_ms) noexcept;

  /// Advances the camera hold; call once per tick before view().
  void tick(std::uint64_t now_ms) noexcept { now_ms_ = now_ms; }

  /// Touch in panel pixels, already known not to be a CarPlay-forwarded touch. Returns true
  /// when the surfaces consumed it (tabs, the theme button, a settings page, the camera);
  /// false hands it to the dock. Buttons activate on a release over the pressed control;
  /// sliders follow the finger and commit on release; Cancel rolls a slider back.
  [[nodiscard]] bool on_touch(const platform::TouchEvent& ev) noexcept;

  /// Development aid (--open): show a surface without touching. Names: camera, settings,
  /// audio, theme, units, calibration, system. Unknown names are ignored and reported false.
  bool open_surface(std::string_view name) noexcept;

  /// True while CarPlay owns the well, so touches inside it are forwarded to the dongle.
  [[nodiscard]] bool carplay_active() const noexcept { return !camera_showing() && page_ == render::Page::None; }
  /// Reverse, the hold after it, or the Camera tab.
  [[nodiscard]] bool camera_showing() const noexcept;
  /// The settings page, kept even while the camera covers it.
  [[nodiscard]] render::Page page() const noexcept { return page_; }
  [[nodiscard]] const render::Settings& settings() const noexcept { return settings_; }
  [[nodiscard]] const audio::AudioState& audio() const noexcept { return audio_; }
  [[nodiscard]] bool dark() const noexcept { return render::effective_dark(settings_, headlights_); }
  [[nodiscard]] render::Theme theme() const noexcept { return render::make_theme(settings_, headlights_); }

  /// Set once per theme-affecting change; the loop hands the new theme to the scene.
  [[nodiscard]] std::optional<render::Theme> take_theme_change() noexcept;

  /// Set once per committed settings change (a tap, or a hue drag's release); the loop
  /// persists it. Live hue dragging does not count.
  [[nodiscard]] std::optional<render::Settings> take_settings_change() noexcept;

  [[nodiscard]] render::WellView view(const telemetry::Frame* frame, bool live,
                                      telemetry::LinkState link) const noexcept;

private:
  enum class Press : std::uint8_t {
    None,
    Tab,
    Tool,
    SheetControl,
    SheetPanel,  // inside the settings surface, not on a control
    Camera,
    CameraPicture,
  };

  [[nodiscard]] const render::SheetLayout& sheet_layout() const noexcept {
    return sheets_[static_cast<std::size_t>(page_)];
  }

  [[nodiscard]] render::CameraState camera_state() const noexcept;
  [[nodiscard]] bool car_owns_well() const noexcept { return reverse_ || now_ms_ < hold_until_ms_; }

  void press_down(const platform::TouchEvent& ev) noexcept;
  void release(const platform::TouchEvent& ev) noexcept;
  void activate_tab(render::Tab tab) noexcept;
  void activate_tool(render::Tool tool) noexcept;
  void activate_control(render::SheetControl c) noexcept;
  void set_slider(render::SheetControl c, float x) noexcept;
  void show_page(render::Page p) noexcept;
  void settings_changed() noexcept;
  void audio_changed() noexcept;

private:
  render::Layout layout_;
  render::TabsLayout tabs_;
  render::ToolsLayout tools_;
  render::PageLayouts sheets_;  // indexed by render::Page
  render::CameraLayout camera_;
  const telemetry::CalibrationTable* calibration_;
  audio::AudioSink& audio_sink_;

  render::Settings settings_;
  audio::AudioState audio_{};
  render::Page page_ = render::Page::None;
  bool headlights_ = false;
  bool reverse_ = false;
  bool manual_camera_ = false;
  bool guides_ = true;
  std::uint64_t hold_until_ms_ = 0;
  std::uint64_t now_ms_ = 0;
  bool theme_changed_ = true;  // the first take() delivers the initial theme
  bool settings_changed_ = false;

  render::Settings settings_before_drag_;
  audio::AudioState audio_before_drag_;
  Press press_ = Press::None;
  std::optional<render::Tab> pressed_tab_;
  std::optional<render::Tool> pressed_tool_;
  std::optional<render::SheetControl> pressed_control_;
  std::optional<render::CameraControl> pressed_camera_;
};

}  // namespace c3::app
