// Rear camera surface
// Occupies the well while reverse is engaged, for 2 s after it clears, or as a manual preview.
//
// No camera source exists yet, so the picture is always the "camera unavailable" state and
// the guideline overlay is not drawn: never a frozen or fabricated image (mirrors safety
// invariant 4). The badges and tools are real so the surface's touch behaviour can be built
// and tested now.
#pragma once

#include "canvas.hpp"
#include "fonts.hpp"
#include "layout.hpp"
#include "theme.hpp"

#include <cstdint>
#include <optional>

namespace c3::render {

enum class CameraState : std::uint8_t {
  Preview,  // opened from the status bar
  Reverse,  // `rev` true in a live frame
  Hold,     // reverse cleared less than 2 s ago
};

enum class CameraControl : std::uint8_t {
  Guides,
  Close,  // hidden while Reverse or Hold
};

struct CameraModel {
  CameraState state = CameraState::Preview;
  bool frames = false;  // false: "camera unavailable"
  bool guides = true;
  std::optional<CameraControl> pressed;
};

struct CameraLayout {
  Rect well;
  float scale = 1.0F;
  Rect guides_button;
  Rect close_button;

  [[nodiscard]] static CameraLayout compute(const Rect& well) noexcept;

  /// `state` decides whether the close button exists.
  [[nodiscard]] std::optional<CameraControl> hit_test(float x, float y, CameraState state) const noexcept;
};

void draw_camera(Canvas* vg, const CameraLayout& cl, const Theme& t, const Fonts& f, const CameraModel& m) noexcept;

}  // namespace c3::render
