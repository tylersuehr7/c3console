#include "icons.hpp"

#include <cmath>
#include <numbers>

namespace c3::render {

namespace {

using std::numbers::pi_v;

constexpr float kBox = 24.0F;

[[nodiscard]] constexpr float rad(float deg) noexcept {
  return deg * pi_v<float> / 180.0F;
}

void line(Canvas* vg, float x0, float y0, float x1, float y1) noexcept {
  nvgMoveTo(vg, x0, y0);
  nvgLineTo(vg, x1, y1);
}

// M4 9v6h4l5 4V5L8 9H4z
void speaker_body(Canvas* vg) noexcept {
  nvgMoveTo(vg, 4.0F, 9.0F);
  nvgLineTo(vg, 4.0F, 15.0F);
  nvgLineTo(vg, 8.0F, 15.0F);
  nvgLineTo(vg, 13.0F, 19.0F);
  nvgLineTo(vg, 13.0F, 5.0F);
  nvgLineTo(vg, 8.0F, 9.0F);
  nvgClosePath(vg);
}

// M3 8h4l2-3h6l2 3h4v11H3z + lens
void camera_body(Canvas* vg) noexcept {
  nvgMoveTo(vg, 3.0F, 8.0F);
  nvgLineTo(vg, 7.0F, 8.0F);
  nvgLineTo(vg, 9.0F, 5.0F);
  nvgLineTo(vg, 15.0F, 5.0F);
  nvgLineTo(vg, 17.0F, 8.0F);
  nvgLineTo(vg, 21.0F, 8.0F);
  nvgLineTo(vg, 21.0F, 19.0F);
  nvgLineTo(vg, 3.0F, 19.0F);
  nvgClosePath(vg);
  nvgCircle(vg, 12.0F, 13.0F, 3.5F);
}

// Eight spokes between two radii, every 45°.
void spokes(Canvas* vg, float r0, float r1) noexcept {
  for (int i = 0; i < 8; ++i) {
    const float a = rad(static_cast<float>(i) * 45.0F);
    line(vg, 12.0F + std::cos(a) * r0, 12.0F + std::sin(a) * r0, 12.0F + std::cos(a) * r1, 12.0F + std::sin(a) * r1);
  }
}

void path_for(Canvas* vg, Icon icon) noexcept {
  switch (icon) {
    case Icon::Speaker:
      speaker_body(vg);
      // M16 9a4 4 0 0 1 0 6 ; M18.5 6.5a8 8 0 0 1 0 11 (centre solved from the chord)
      nvgMoveTo(vg, 16.0F, 9.0F);
      nvgArc(vg, 16.0F, 12.0F, 3.0F, rad(-90.0F), rad(90.0F), NVG_CW);
      nvgMoveTo(vg, 18.5F, 6.5F);
      nvgArc(vg, 12.7F, 12.0F, 8.0F, rad(-43.5F), rad(43.5F), NVG_CW);
      break;
    case Icon::SpeakerMuted:
      speaker_body(vg);
      line(vg, 17.0F, 9.0F, 22.0F, 15.0F);
      line(vg, 22.0F, 9.0F, 17.0F, 15.0F);
      break;
    case Icon::Camera:
      camera_body(vg);
      break;
    case Icon::CameraOff:
      camera_body(vg);
      line(vg, 3.0F, 3.0F, 21.0F, 21.0F);
      break;
    case Icon::Gear:
      // The mockup's cog outline simplified to hub, rim and eight teeth.
      nvgCircle(vg, 12.0F, 12.0F, 3.0F);
      nvgCircle(vg, 12.0F, 12.0F, 7.0F);
      spokes(vg, 7.0F, 10.0F);
      break;
    case Icon::Sun:
      nvgCircle(vg, 12.0F, 12.0F, 4.0F);
      spokes(vg, 8.0F, 10.0F);
      break;
    case Icon::Moon:
      // M20 14.5A8 8 0 0 1 9.5 4a8 8 0 1 0 10.5 10.5z: outer arc about the box centre, inner
      // arc about (12.64, 11.36), both solved from the SVG chords.
      nvgMoveTo(vg, 20.0F, 14.5F);
      nvgArc(vg, 12.0F, 12.0F, 8.0F, rad(17.35F), rad(252.65F), NVG_CW);
      nvgArc(vg, 12.64F, 11.36F, 8.0F, rad(246.9F), rad(23.1F), NVG_CCW);
      nvgClosePath(vg);
      break;
    case Icon::Close:
      line(vg, 6.0F, 6.0F, 18.0F, 18.0F);
      line(vg, 18.0F, 6.0F, 6.0F, 18.0F);
      break;
    case Icon::Guides:
      // M4 20L9 5h6l5 15 M6.5 14h11 M8 9.5h8
      nvgMoveTo(vg, 4.0F, 20.0F);
      nvgLineTo(vg, 9.0F, 5.0F);
      nvgLineTo(vg, 15.0F, 5.0F);
      nvgLineTo(vg, 20.0F, 20.0F);
      line(vg, 6.5F, 14.0F, 17.5F, 14.0F);
      line(vg, 8.0F, 9.5F, 16.0F, 9.5F);
      break;
    case Icon::Calibrate:
      // M4 20L20 4 M4 12h4 M12 4v4 M4 16h2 M16 4v2 M8 4v2 M4 8h2
      line(vg, 4.0F, 20.0F, 20.0F, 4.0F);
      line(vg, 4.0F, 12.0F, 8.0F, 12.0F);
      line(vg, 12.0F, 4.0F, 12.0F, 8.0F);
      line(vg, 4.0F, 16.0F, 6.0F, 16.0F);
      line(vg, 16.0F, 4.0F, 16.0F, 6.0F);
      line(vg, 8.0F, 4.0F, 8.0F, 6.0F);
      line(vg, 4.0F, 8.0F, 6.0F, 8.0F);
      break;
    case Icon::Reset:
      // M3 12a9 9 0 1 0 3-6.7L3 8 M3 3v5h5
      nvgMoveTo(vg, 3.0F, 12.0F);
      nvgArc(vg, 12.0F, 12.0F, 9.0F, rad(180.0F), rad(-131.8F), NVG_CCW);
      nvgLineTo(vg, 3.0F, 8.0F);
      nvgMoveTo(vg, 3.0F, 3.0F);
      nvgLineTo(vg, 3.0F, 8.0F);
      nvgLineTo(vg, 8.0F, 8.0F);
      break;
    case Icon::Phone:
      // rect x5 y2 w14 h20 rx3 ; M11 18h2
      nvgRoundedRect(vg, 5.0F, 2.0F, 14.0F, 20.0F, 3.0F);
      line(vg, 11.0F, 18.0F, 13.0F, 18.0F);
      break;
    case Icon::Back:
      // M15 5l-7 7 7 7
      nvgMoveTo(vg, 15.0F, 5.0F);
      nvgLineTo(vg, 8.0F, 12.0F);
      nvgLineTo(vg, 15.0F, 19.0F);
      break;
    case Icon::ChevronRight:
      // M9 5l7 7-7 7
      nvgMoveTo(vg, 9.0F, 5.0F);
      nvgLineTo(vg, 16.0F, 12.0F);
      nvgLineTo(vg, 9.0F, 19.0F);
      break;
    case Icon::Contrast:
      // half-disc: the outline with the right half suggested by three horizontal rules
      nvgCircle(vg, 12.0F, 12.0F, 9.0F);
      line(vg, 12.0F, 3.0F, 12.0F, 21.0F);
      line(vg, 12.0F, 7.0F, 18.5F, 7.0F);
      line(vg, 12.0F, 12.0F, 21.0F, 12.0F);
      line(vg, 12.0F, 17.0F, 18.5F, 17.0F);
      break;
    case Icon::Target:
      // M12 3v3 M12 18v3 M3 12h3 M18 12h3 ; circle r6 ; centre dot
      line(vg, 12.0F, 3.0F, 12.0F, 6.0F);
      line(vg, 12.0F, 18.0F, 12.0F, 21.0F);
      line(vg, 3.0F, 12.0F, 6.0F, 12.0F);
      line(vg, 18.0F, 12.0F, 21.0F, 12.0F);
      nvgCircle(vg, 12.0F, 12.0F, 6.0F);
      nvgCircle(vg, 12.0F, 12.0F, 1.2F);
      break;
    case Icon::Monitor:
      // rect x3 y4 w18 h12 rx2 ; M8 20h8 ; M12 16v4
      nvgRoundedRect(vg, 3.0F, 4.0F, 18.0F, 12.0F, 2.0F);
      line(vg, 8.0F, 20.0F, 16.0F, 20.0F);
      line(vg, 12.0F, 16.0F, 12.0F, 20.0F);
      break;
    case Icon::Doors:
      // M3 14c2.5-4.5 5.5-7 9-7s6.5 2.5 9 7 ; M3 18h18 ; M12 7V4
      nvgMoveTo(vg, 3.0F, 14.0F);
      nvgBezierTo(vg, 5.5F, 9.5F, 8.5F, 7.0F, 12.0F, 7.0F);
      nvgBezierTo(vg, 15.5F, 7.0F, 18.5F, 9.5F, 21.0F, 14.0F);
      line(vg, 3.0F, 18.0F, 21.0F, 18.0F);
      line(vg, 12.0F, 7.0F, 12.0F, 4.0F);
      break;
    case Icon::Headlamp:
      // M9 6a6 6 0 1 0 0 12h3V6H9z ; M15 8h6 M15 12h6 M15 16h6
      nvgMoveTo(vg, 12.0F, 6.0F);
      nvgLineTo(vg, 9.0F, 6.0F);
      nvgArc(vg, 9.0F, 12.0F, 6.0F, rad(-90.0F), rad(90.0F), NVG_CCW);
      nvgLineTo(vg, 12.0F, 18.0F);
      nvgClosePath(vg);
      line(vg, 15.0F, 8.0F, 21.0F, 8.0F);
      line(vg, 15.0F, 12.0F, 21.0F, 12.0F);
      line(vg, 15.0F, 16.0F, 21.0F, 16.0F);
      break;
    case Icon::HighBeam:
      // same lamp, rays fanning out: M15 7l6-2 M15 12h6 M15 17l6 2
      nvgMoveTo(vg, 12.0F, 6.0F);
      nvgLineTo(vg, 9.0F, 6.0F);
      nvgArc(vg, 9.0F, 12.0F, 6.0F, rad(-90.0F), rad(90.0F), NVG_CCW);
      nvgLineTo(vg, 12.0F, 18.0F);
      nvgClosePath(vg);
      line(vg, 15.0F, 7.0F, 21.0F, 5.0F);
      line(vg, 15.0F, 12.0F, 21.0F, 12.0F);
      line(vg, 15.0F, 17.0F, 21.0F, 19.0F);
      break;
    case Icon::Snowflake:
      // M12 2v20 M2 12h20 M4.9 4.9l14.2 14.2 M19.1 4.9L4.9 19.1
      line(vg, 12.0F, 2.0F, 12.0F, 22.0F);
      line(vg, 2.0F, 12.0F, 22.0F, 12.0F);
      line(vg, 4.9F, 4.9F, 19.1F, 19.1F);
      line(vg, 19.1F, 4.9F, 4.9F, 19.1F);
      break;
  }
}

}  // namespace

void draw_icon(Canvas* vg, Icon icon, float cx, float cy, float size, Color stroke, float stroke_units) noexcept {
  const float s = size / kBox;
  nvgSave(vg);
  nvgTranslate(vg, cx - size / 2.0F, cy - size / 2.0F);
  nvgScale(vg, s, s);
  nvgBeginPath(vg);
  path_for(vg, icon);
  nvgStrokeColor(vg, stroke);
  nvgStrokeWidth(vg, stroke_units);
  nvgLineCap(vg, NVG_ROUND);
  nvgLineJoin(vg, NVG_ROUND);
  nvgStroke(vg);
  nvgRestore(vg);
}

}  // namespace c3::render
