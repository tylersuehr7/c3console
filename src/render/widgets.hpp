#pragma once

#include "canvas.hpp"
#include "fonts.hpp"
#include "icons.hpp"
#include "layout.hpp"
#include "theme.hpp"

#include <cstddef>
#include <span>
#include <string_view>

namespace c3::render::widgets {

// Reference sizes, mockup units.
constexpr float kControlRadius = 24.0F;  // --r-ctl
constexpr float kTileRadius = 32.0F;     // --r-tile
constexpr float kSegmentRadius = 18.0F;
constexpr float kSegmentPad = 6.0F;
constexpr float kSegmentGap = 6.0F;

/// Splits a segmented container into equal buttons, inset by the segment padding.
void split_segments(const Rect& group, float scale, std::span<Rect> out) noexcept;

/// One button of a segmented control. The container (fill + hairline) is drawn by the caller
/// with draw::rounded_fill / rounded_stroke at kControlRadius.
void segment_button(Canvas* vg, const Fonts& f, const Theme& t, float scale, const Rect& r, std::string_view caption,
                    bool on, bool pressed) noexcept;

/// Title with a small tracked state line beneath, optional leading icon.
void toggle(Canvas* vg, const Fonts& f, const Theme& t, float scale, const Rect& r, std::string_view title,
            std::string_view state, bool on, bool pressed, const Icon* icon = nullptr) noexcept;

/// Round icon button (.iconbtn): surface fill, hairline, 32-unit glyph; ember when `on`.
void icon_button(Canvas* vg, const Theme& t, const Rect& r, Icon icon, bool on, bool pressed,
                 bool alarm_ink = false) noexcept;

struct SliderStyle {
  float track_h = 12.0F;
  float thumb_d = 52.0F;
  bool fill_from_start = false;  // ember fill up to the thumb (volume)
  bool centre_tick = false;      // zero mark under the track (balance, fader, tone)
};

/// Horizontal range control: track across `r` at its vertical centre, thumb at `frac` (0..1).
void slider(Canvas* vg, const Theme& t, float scale, const Rect& r, float frac, const SliderStyle& style,
            Color sheet_bg) noexcept;

/// Hue slider track: the spectrum at 92 % saturation, 58 % lightness.
void hue_track(Canvas* vg, float scale, const Rect& r, float frac, Color thumb, Color sheet_bg) noexcept;

/// Colour swatch (.sw): filled disc with an ink ring when selected.
void swatch(Canvas* vg, const Theme& t, const Rect& r, Color color, bool on) noexcept;

/// Fraction of `r`'s width at `x`, clamped to 0..1: the touch side of slider().
[[nodiscard]] float slider_fraction(const Rect& r, float x) noexcept;

}  // namespace c3::render::widgets
