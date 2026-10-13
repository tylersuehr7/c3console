#include "camera_surface.hpp"

#include "draw.hpp"
#include "icons.hpp"

namespace c3::render {

namespace {

constexpr float kRefW = 1520.0F;  // v6 well width
constexpr float kWellRadius = 28.0F;
constexpr float kInset = 24.0F;
constexpr float kButton = 72.0F;
constexpr float kGap = 12.0F;
constexpr float kBadgeH = 64.0F;

[[nodiscard]] constexpr std::string_view state_text(CameraState s) noexcept {
  switch (s) {
    case CameraState::Preview:
      return "PREVIEW";
    case CameraState::Reverse:
      return "REVERSE";
    case CameraState::Hold:
      return "REVERSE \xC2\xB7 HOLD";
  }
  return "";
}

// Pill badge with optional icon; returns its width.
float badge(Canvas* vg, const Fonts& f, float s, float x, float y, std::string_view text, Color fill, Color border,
            bool with_icon) noexcept {
  const float pad = 24.0F * s;
  const float icon_w = with_icon ? (30.0F + 12.0F) * s : 0.0F;
  const float tw = draw::text_width(vg, f.ui_semibold, 24.0F * s, text, 2.4F * s);
  const Rect r{.x = x, .y = y, .w = pad * 2 + icon_w + tw, .h = kBadgeH * s};
  draw::rounded_fill(vg, r, r.h / 2.0F, fill);
  draw::rounded_stroke(vg, r, r.h / 2.0F, border);
  if (with_icon) {
    draw_icon(vg, Icon::Camera, x + pad + 15.0F * s, r.cy(), 30.0F * s, rgb(0xFFFFFF), 2.2F);
  }
  draw::text(vg, f.ui_semibold, 24.0F * s, rgb(0xFFFFFF), draw::kLeftMiddle, x + pad + icon_w, r.cy(), text, 2.4F * s);
  return r.w;
}

void tool_button(Canvas* vg, const Rect& r, Icon icon, bool on, bool pressed) noexcept {
  draw::circle_fill(vg, r.cx(), r.cy(), r.w / 2.0F, on ? rgb(0xFFFFFF) : rgba(0x000000, pressed ? 0.75F : 0.55F));
  draw::circle_hairline(vg, r.cx(), r.cy(), r.w / 2.0F, on ? rgb(0xFFFFFF) : rgba(0xFFFFFF, 0.12F));
  draw_icon(vg, icon, r.cx(), r.cy(), r.w / 2.0F, on ? rgb(0x000000) : rgb(0xFFFFFF));
}

}  // namespace

CameraLayout CameraLayout::compute(const Rect& well) noexcept {
  const float s = well.w > 0 ? well.w / kRefW : 1.0F;
  const float d = kButton * s;
  const float y = well.y + kInset * s;
  return {.well = well,
          .scale = s,
          .guides_button = {.x = well.right() - kInset * s - 2 * d - kGap * s, .y = y, .w = d, .h = d},
          .close_button = {.x = well.right() - kInset * s - d, .y = y, .w = d, .h = d}};
}

std::optional<CameraControl> CameraLayout::hit_test(float x, float y, CameraState state) const noexcept {
  if (guides_button.contains(x, y)) {
    return CameraControl::Guides;
  }
  if (state == CameraState::Preview && close_button.contains(x, y)) {
    return CameraControl::Close;
  }
  return std::nullopt;
}

void draw_camera(Canvas* vg, const CameraLayout& cl, const Theme& t, const Fonts& f, const CameraModel& m) noexcept {
  const float s = cl.scale;
  const Rect& r = cl.well;
  const draw::State st(vg);
  st.clip(r);

  // Picture area. Without frames: the mockup's #camfault panel. The camera's own chrome is
  // fixed dark like the CarPlay well; it does not follow the theme.
  draw::rounded_fill(vg, r, kWellRadius * s, m.frames ? rgb(0x000000) : rgb(0x05060A));
  if (!m.frames) {
    const float cx = r.cx();
    const float cy = r.cy() - 40.0F * s;
    draw_icon(vg, Icon::CameraOff, cx, cy - 70.0F * s, 96.0F * s, t.alarm);
    draw::text(vg, f.ui_medium, 40.0F * s, rgb(0xF5F3EE), draw::kCenterMiddle, cx, cy + 30.0F * s,
               "Camera unavailable");
    draw::text(vg, f.ui_medium, 26.0F * s, rgb(0x5B6068), draw::kCenterMiddle, cx, cy + 84.0F * s,
               "NO FRAMES FROM REAR CAMERA \xC2\xB7 USE MIRRORS", 1.6F * s);
  }

  // badges, top-left
  const float bx = r.x + kInset * s;
  const float by = r.y + kInset * s;
  const float w1 = badge(vg, f, s, bx, by, "REAR CAMERA", rgba(0x000000, 0.55F), rgba(0xFFFFFF, 0.12F), true);
  const bool rev = m.state != CameraState::Preview;
  badge(vg, f, s, bx + w1 + kGap * s, by, state_text(m.state), rev ? t.alarm : rgba(0x000000, 0.55F),
        rev ? t.alarm : rgba(0xFFFFFF, 0.12F), false);

  // tools, top-right
  tool_button(vg, cl.guides_button, Icon::Guides, m.guides, m.pressed == CameraControl::Guides);
  if (!rev) {
    tool_button(vg, cl.close_button, Icon::Close, false, m.pressed == CameraControl::Close);
  }
}

}  // namespace c3::render
