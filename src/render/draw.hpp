#pragma once

#include "canvas.hpp"
#include "layout.hpp"

#include <cstdint>
#include <string_view>

namespace c3::render::draw {

// Text alignment vocabulary: horizontal × vertical anchor.
constexpr int kLeftTop = NVG_ALIGN_LEFT | NVG_ALIGN_TOP;
constexpr int kLeftMiddle = NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE;
constexpr int kLeftBaseline = NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE;
constexpr int kCenterMiddle = NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE;
constexpr int kCenterBaseline = NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE;
constexpr int kRightMiddle = NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE;
constexpr int kRightBaseline = NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE;

enum class Cap : std::uint8_t { Butt, Round };

/// Saves the canvas state for the enclosing scope and restores it on exit. Transform, clip
/// and alpha changes made through it cannot leak into the next thing drawn.
class State {
public:
  explicit State(Canvas* vg) noexcept : vg_(vg) { nvgSave(vg_); }
  ~State() { nvgRestore(vg_); }
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  State(State&&) = delete;
  State& operator=(State&&) = delete;

  void translate(float x, float y) const noexcept { nvgTranslate(vg_, x, y); }
  void scale(float s) const noexcept { nvgScale(vg_, s, s); }
  void global_alpha(float a) const noexcept { nvgGlobalAlpha(vg_, a); }
  /// Intersects the clip with `r`; everything drawn until the scope ends stays inside it.
  void clip(const Rect& r) const noexcept { nvgIntersectScissor(vg_, r.x, r.y, r.w, r.h); }

private:
  Canvas* vg_;
};

// ---------------------------------------------------------------------------
// colors and paints
// ---------------------------------------------------------------------------

/// `c` with its alpha multiplied by `a`.
[[nodiscard]] inline Color alpha(Color c, float a) noexcept {
  return nvgTransRGBAf(c, a);
}

[[nodiscard]] inline Paint linear(Canvas* vg, float x0, float y0, float x1, float y1, Color c0, Color c1) noexcept {
  return nvgLinearGradient(vg, x0, y0, x1, y1, c0, c1);
}

[[nodiscard]] inline Paint radial(Canvas* vg, float cx, float cy, float r0, float r1, Color c0, Color c1) noexcept {
  return nvgRadialGradient(vg, cx, cy, r0, r1, c0, c1);
}

/// Rounded-box gradient: `c0` inside, fading to `c1` over `feather` at the edge.
[[nodiscard]] inline Paint box(Canvas* vg, const Rect& r, float radius, float feather, Color c0, Color c1) noexcept {
  return nvgBoxGradient(vg, r.x, r.y, r.w, r.h, radius, feather, c0, c1);
}

// ---------------------------------------------------------------------------
// fills
// ---------------------------------------------------------------------------

inline void rect_fill(Canvas* vg, const Rect& r, Color c) noexcept {
  nvgBeginPath(vg);
  nvgRect(vg, r.x, r.y, r.w, r.h);
  nvgFillColor(vg, c);
  nvgFill(vg);
}

inline void rect_fill(Canvas* vg, const Rect& r, const Paint& p) noexcept {
  nvgBeginPath(vg);
  nvgRect(vg, r.x, r.y, r.w, r.h);
  nvgFillPaint(vg, p);
  nvgFill(vg);
}

inline void rounded_fill(Canvas* vg, const Rect& r, float radius, Color c) noexcept {
  nvgBeginPath(vg);
  nvgRoundedRect(vg, r.x, r.y, r.w, r.h, radius);
  nvgFillColor(vg, c);
  nvgFill(vg);
}

inline void rounded_fill(Canvas* vg, const Rect& r, float radius, const Paint& p) noexcept {
  nvgBeginPath(vg);
  nvgRoundedRect(vg, r.x, r.y, r.w, r.h, radius);
  nvgFillPaint(vg, p);
  nvgFill(vg);
}

inline void circle_fill(Canvas* vg, float cx, float cy, float r, Color c) noexcept {
  nvgBeginPath(vg);
  nvgCircle(vg, cx, cy, r);
  nvgFillColor(vg, c);
  nvgFill(vg);
}

inline void circle_fill(Canvas* vg, float cx, float cy, float r, const Paint& p) noexcept {
  nvgBeginPath(vg);
  nvgCircle(vg, cx, cy, r);
  nvgFillPaint(vg, p);
  nvgFill(vg);
}

// ---------------------------------------------------------------------------
// strokes
// ---------------------------------------------------------------------------

/// One-pixel hairline inset by half a pixel so it lands on whole pixels.
inline void rounded_stroke(Canvas* vg, const Rect& r, float radius, Color c) noexcept {
  nvgBeginPath(vg);
  nvgRoundedRect(vg, r.x + 0.5F, r.y + 0.5F, r.w - 1.0F, r.h - 1.0F, radius);
  nvgStrokeColor(vg, c);
  nvgStrokeWidth(vg, 1.0F);
  nvgStroke(vg);
}

/// One-pixel hairline just inside a disc of radius `r`.
inline void circle_hairline(Canvas* vg, float cx, float cy, float r, Color c) noexcept {
  nvgBeginPath(vg);
  nvgCircle(vg, cx, cy, r - 0.5F);
  nvgStrokeColor(vg, c);
  nvgStrokeWidth(vg, 1.0F);
  nvgStroke(vg);
}

inline void circle_stroke(Canvas* vg, float cx, float cy, float r, float width, Color c) noexcept {
  nvgBeginPath(vg);
  nvgCircle(vg, cx, cy, r);
  nvgStrokeColor(vg, c);
  nvgStrokeWidth(vg, width);
  nvgStroke(vg);
}

/// Clockwise arc from `a0` to `a1` (radians, NanoVG's y-down convention).
inline void arc_stroke(Canvas* vg, float cx, float cy, float r, float a0, float a1, float width, Color c,
                       Cap cap) noexcept {
  nvgLineCap(vg, cap == Cap::Round ? NVG_ROUND : NVG_BUTT);
  nvgBeginPath(vg);
  nvgArc(vg, cx, cy, r, a0, a1, NVG_CW);
  nvgStrokeColor(vg, c);
  nvgStrokeWidth(vg, width);
  nvgStroke(vg);
}

inline void arc_stroke(Canvas* vg, float cx, float cy, float r, float a0, float a1, float width, const Paint& p,
                       Cap cap) noexcept {
  nvgLineCap(vg, cap == Cap::Round ? NVG_ROUND : NVG_BUTT);
  nvgBeginPath(vg);
  nvgArc(vg, cx, cy, r, a0, a1, NVG_CW);
  nvgStrokePaint(vg, p);
  nvgStrokeWidth(vg, width);
  nvgStroke(vg);
}

/// Straight segment with butt caps: ticks and separators.
inline void line(Canvas* vg, float x0, float y0, float x1, float y1, float width, Color c) noexcept {
  nvgLineCap(vg, NVG_BUTT);
  nvgBeginPath(vg);
  nvgMoveTo(vg, x0, y0);
  nvgLineTo(vg, x1, y1);
  nvgStrokeColor(vg, c);
  nvgStrokeWidth(vg, width);
  nvgStroke(vg);
}

// ---------------------------------------------------------------------------
// text
// ---------------------------------------------------------------------------

inline void text(Canvas* vg, int font, float size, Color color, int align, float x, float y, std::string_view s,
                 float tracking = 0.0F) noexcept {
  nvgFontFaceId(vg, font);
  nvgFontSize(vg, size);
  nvgTextLetterSpacing(vg, tracking);
  nvgTextAlign(vg, align);
  nvgFillColor(vg, color);
  nvgText(vg, x, y, s.data(), s.data() + s.size());
}

[[nodiscard]] inline float text_width(Canvas* vg, int font, float size, std::string_view s,
                                      float tracking = 0.0F) noexcept {
  nvgFontFaceId(vg, font);
  nvgFontSize(vg, size);
  nvgTextLetterSpacing(vg, tracking);
  return nvgTextBounds(vg, 0, 0, s.data(), s.data() + s.size(), nullptr);
}

/// Word-wrapped paragraph anchored top-left at (x, y), `w` wide. `line_height` is relative to
/// the font size.
inline void text_box(Canvas* vg, int font, float size, Color color, float x, float y, float w, float line_height,
                     std::string_view s) noexcept {
  nvgFontFaceId(vg, font);
  nvgFontSize(vg, size);
  nvgTextLetterSpacing(vg, 0.0F);
  nvgTextLineHeight(vg, line_height);
  nvgTextAlign(vg, kLeftTop);
  nvgFillColor(vg, color);
  nvgTextBox(vg, x, y, w, s.data(), s.data() + s.size());
}

}  // namespace c3::render::draw
