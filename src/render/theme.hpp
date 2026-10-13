// Theme — color tokens. Runtime-switchable: dark, light, and an accent that recolors
// only what is selected or interactive. Gauges stay monochrome and warnings stay
// red/amber whatever the accent, so meaning survives.
#pragma once

#include "canvas.hpp"

#include <cstdint>

namespace c3::render {

/// 0xRRGGBB -> opaque colour.
[[nodiscard]] Color rgb(std::uint32_t hex) noexcept;
/// 0xRRGGBB with alpha 0..1.
[[nodiscard]] Color rgba(std::uint32_t hex, float alpha) noexcept;
/// WCAG relative luminance, 0..1.
[[nodiscard]] float luminance(Color c) noexcept;

struct Theme {
  // surfaces and text
  Color bg;
  Color surface;
  Color surface2;
  Color line;
  Color line2;
  Color ink;
  Color ink2;
  Color ink3;
  // semantics
  Color ember;      // interactive / selected only
  Color ember_ink;  // text on ember
  Color amber;      // caution
  Color amber_ink;
  Color alarm;  // critical
  Color ok;     // link dot
  // gauges
  Color track;
  Color band;
  Color zone;
  Color fill0;
  Color fill1;
  Color marker;
  Color glow;
  Color halo;
  // banners
  Color banner_neutral;
  Color banner_neutral_ink;
  // CarPlay well chrome (the phone owns what is inside)
  Color well_bg;
  Color sheet;  // audio / settings sheet panel

  [[nodiscard]] static Theme dark() noexcept;
  [[nodiscard]] static Theme light() noexcept;

  /// Re-values ember and picks a contrasting ember_ink.
  void set_accent(Color accent) noexcept;
};

}  // namespace c3::render
