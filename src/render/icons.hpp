#pragma once

#include "canvas.hpp"

#include <cstdint>

namespace c3::render {

enum class Icon : std::uint8_t {
  Speaker,
  SpeakerMuted,
  Camera,
  CameraOff,
  Gear,
  Sun,
  Moon,
  Close,
  Guides,
  Calibrate,
  Reset,
  Phone,         // CarPlay tab
  Back,          // chevron left: nested settings
  ChevronRight,  // settings rows
  Contrast,      // theme row
  Target,        // calibration row
  Monitor,       // system row
  Doors,         // headlight doors
  Headlamp,      // lights
  HighBeam,
  Snowflake,  // A/C
};

/// Draws `icon` centred on (cx, cy) inside a size × size box with the given stroke color.
/// Stroke width follows the mockup: 2 units of the 24-unit box.
void draw_icon(Canvas* vg, Icon icon, float cx, float cy, float size, Color stroke, float stroke_units = 2.0F) noexcept;

}  // namespace c3::render
