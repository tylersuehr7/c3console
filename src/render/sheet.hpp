#pragma once

#include "audio/audio_state.hpp"
#include "canvas.hpp"
#include "fonts.hpp"
#include "layout.hpp"
#include "platform/display_backend.hpp"
#include "settings.hpp"
#include "telemetry/calibration.hpp"
#include "telemetry/frame.hpp"
#include "telemetry/sensor_source.hpp"
#include "theme.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace c3::render {

enum class Page : std::uint8_t {
  None,  // settings surface not showing
  Root,
  Audio,
  Theme,
  Units,
  Calibration,
  System,
  Count,
};
constexpr std::size_t kPageCount = static_cast<std::size_t>(Page::Count);

enum class SheetControl : std::uint8_t {
  Close,
  Back,
  // root rows
  RowAudio,
  RowTheme,
  RowUnits,
  RowCalibration,
  RowSystem,
  // audio
  Volume,
  Mute,
  Balance,
  Fader,
  Sub,
  Bass,
  Mid,
  Treble,
  PresetFlat,
  PresetRock,
  PresetCruise,
  PresetVoice,
  // theme
  ThemeDark,
  ThemeLight,
  ThemeAuto,
  Swatch0,
  Swatch1,
  Swatch2,
  Swatch3,
  Swatch4,
  Swatch5,
  Swatch6,
  Swatch7,
  Swatch8,
  Hue,
  // units
  TempF,
  TempC,
  PressPsi,
  PressBar,
  // system
  Reset,
  Count,
};
constexpr std::size_t kSheetControlCount = static_cast<std::size_t>(SheetControl::Count);

/// Sliders take continuous input while pressed; everything else activates on release.
[[nodiscard]] constexpr bool is_slider(SheetControl c) noexcept {
  switch (c) {
    case SheetControl::Volume:
    case SheetControl::Balance:
    case SheetControl::Fader:
    case SheetControl::Sub:
    case SheetControl::Bass:
    case SheetControl::Mid:
    case SheetControl::Treble:
    case SheetControl::Hue:
      return true;
    default:
      return false;
  }
}

/// The page a root row opens.
[[nodiscard]] constexpr std::optional<Page> page_for(SheetControl c) noexcept {
  switch (c) {
    case SheetControl::RowAudio:
      return Page::Audio;
    case SheetControl::RowTheme:
      return Page::Theme;
    case SheetControl::RowUnits:
      return Page::Units;
    case SheetControl::RowCalibration:
      return Page::Calibration;
    case SheetControl::RowSystem:
      return Page::System;
    default:
      return std::nullopt;
  }
}

struct SheetLayout {
  Page kind = Page::None;
  Rect well;
  platform::Size panel;
  float scale = 1.0F;                             // well pixels per mockup unit (1520-wide reference)
  Rect frame;                                     // the whole well
  std::array<Rect, kSheetControlCount> controls;  // empty rect = not on this page

  // draw-side geometry
  std::array<Rect, 6> cards;
  std::size_t card_count = 0;
  Rect preset_group;
  Rect theme_group;
  Rect temp_group;
  Rect press_group;
  Rect preview;  // accent preview row (theme page)
  Rect kv;       // console key-value block (system page)
  Rect kv2;      // sensor-board key-value block
  Rect banner;   // calibration placeholder banner
  Rect powered;  // "powered by" row

  [[nodiscard]] static SheetLayout compute(Page kind, const Rect& well, platform::Size panel) noexcept;
  [[nodiscard]] std::optional<SheetControl> hit_test(float x, float y) const noexcept;
  [[nodiscard]] const Rect& rect(SheetControl c) const noexcept { return controls[static_cast<std::size_t>(c)]; }
  [[nodiscard]] bool has(SheetControl c) const noexcept { return rect(c).w > 0.0F; }
};

struct SheetModel {
  audio::AudioState audio;
  Settings settings;
  std::optional<SheetControl> pressed;
  telemetry::LinkState link = telemetry::LinkState::NoData;
  // calibration page: raw counts beside table and MCU values
  const telemetry::CalibrationTable* calibration = nullptr;
  const telemetry::Frame* frame = nullptr;  // may be null
  bool live = false;
};

/// One layout per Page, indexed by the enumerator; computed once at init.
using PageLayouts = std::array<SheetLayout, kPageCount>;
[[nodiscard]] PageLayouts compute_pages(const Layout& layout) noexcept;

void draw_sheet(Canvas* vg, const SheetLayout& sl, const Theme& t, const Fonts& f, const SheetModel& m) noexcept;

}  // namespace c3::render
