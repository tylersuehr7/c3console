#include "console_scene.hpp"

#include "brand.hpp"
#include "draw.hpp"
#include "telemetry/fault_codes.hpp"
#include "utils/fixed_format.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace c3::render {
namespace {

using telemetry::Channel;
using telemetry::FaultKind;

// ---------------------------------------------------------------------------
// text helpers
// ---------------------------------------------------------------------------
constexpr float kTracking = 3.0F;  // letter spacing for small caps labels (mockup: 3 px)
constexpr std::string_view kDashes = "\xE2\x80\x94 \xE2\x80\x94";  // "— —"

// 0..1 position of `v` on a scale, clamped to the ends.
[[nodiscard]] constexpr float normalized(float v, float lo, float hi) noexcept {
  return std::clamp((v - lo) / (hi - lo), 0.0F, 1.0F);
}

struct Point {
  float x;
  float y;
};

[[nodiscard]] Point polar(float cx, float cy, float angle, float radius) noexcept {
  return {.x = cx + std::cos(angle) * radius, .y = cy + std::sin(angle) * radius};
}

// ---------------------------------------------------------------------------
// gauge model: what to draw for one channel, derived from faults[] and the live flag
// ---------------------------------------------------------------------------

struct GaugeValue {
  float value = 0.0F;   // numeral
  float needle = 0.0F;  // damped
  GaugeStatus status;
  Severity severity = Severity::Alarm;
};

// ---------------------------------------------------------------------------
// tile chrome
// ---------------------------------------------------------------------------

constexpr float kTileRadius = 32.0F;
constexpr float kTileRefW = 512.0F;  // mockup tile width; arcs and bars are drawn in this space
constexpr float kArcRefH = 544.0F;
constexpr float kBarRefH = 264.0F;

void draw_tile(Canvas* vg, const Rect& r, const Theme& t, GaugeState state, Severity severity) noexcept {
  draw::rounded_fill(vg, r, kTileRadius, t.surface);

  Color border = state == GaugeState::Stale ? t.line2 : t.line;
  if (state == GaugeState::SenderFault || (state == GaugeState::Warn && severity == Severity::Alarm)) {
    border = draw::alpha(t.alarm, 0.6F);
    // inset glow, the mockup's box-shadow
    draw::rounded_fill(vg, r, kTileRadius,
                       draw::box(vg, r, kTileRadius, 120.0F, draw::alpha(t.alarm, 0.0F), draw::alpha(t.alarm, 0.10F)));
  } else if (state == GaugeState::Warn) {
    border = draw::alpha(t.amber, 0.5F);
  }

  draw::rounded_stroke(vg, r, kTileRadius, border);
}

// Enter the mockup's tile coordinate system: origin at the tile, uniform scale, centred.
void enter_tile_space(const draw::State& st, const Rect& r, float ref_h) noexcept {
  const float s = std::min(r.w / kTileRefW, r.h / ref_h);
  st.translate(r.x + (r.w - kTileRefW * s) / 2, r.y + (r.h - ref_h * s) / 2);
  st.scale(s);
}

// ---------------------------------------------------------------------------
// arc gauge (oil, coolant) — mockup arcGauge()
// ---------------------------------------------------------------------------

struct ArcSpec {
  std::string_view label;
  std::string_view unit;
  float min;
  float max;
  float major;
  int decimals;
  float zone_lo;
  float zone_hi;
};

void draw_arc_gauge(Canvas* vg, const Rect& r, const ArcSpec& spec, const GaugeValue& g, const Theme& t,
                    const Fonts& f) noexcept {
  using std::numbers::pi_v;
  draw_tile(vg, r, t, g.status.state, g.severity);
  const draw::State st(vg);
  enter_tile_space(st, r, kArcRefH);
  if (g.status.state == GaugeState::Stale) {
    st.global_alpha(0.55F);
  }

  const float cx = kTileRefW / 2;
  const float cy = 330.0F;
  const float radius = 218.0F;
  const float a0 = pi_v<float> * 5.0F / 6.0F;
  const float sweep = pi_v<float> * 4.0F / 3.0F;
  const float a1 = a0 + sweep;
  const auto ang = [&](float v) noexcept { return a0 + sweep * normalized(v, spec.min, spec.max); };
  const GaugeState state = g.status.state;
  const bool hot = state == GaugeState::Warn;
  const Color warn_color = g.severity == Severity::Caution ? t.amber : t.alarm;

  // track
  draw::arc_stroke(vg, cx, cy, radius, a0, a1, 18.0F, t.track, draw::Cap::Round);

  // danger zone, flush on the track
  if (state != GaugeState::Stale) {
    draw::arc_stroke(vg, cx, cy, radius, ang(spec.zone_lo), ang(spec.zone_hi), 18.0F, t.zone, draw::Cap::Butt);
  }

  // major ticks and scale numerals, inside the track
  const int steps = static_cast<int>(std::lround((spec.max - spec.min) / spec.major));
  std::array<char, 16> num{};
  for (int i = 0; i <= steps; ++i) {
    const float v = spec.min + static_cast<float>(i) * spec.major;
    const float a = ang(v);
    const Point p1 = polar(cx, cy, a, radius - 16.0F);
    const Point p2 = polar(cx, cy, a, radius - 26.0F);
    draw::line(vg, p1.x, p1.y, p2.x, p2.y, 2.0F, t.ink3);
    const Point tp = polar(cx, cy, a, radius - 50.0F);
    const bool whole = std::fabs(v - std::round(v)) < 0.01F;
    draw::text(vg, f.num_medium, 24.0F, state == GaugeState::Stale ? t.ink3 : t.ink2, draw::kCenterMiddle, tp.x, tp.y,
               utils::format_fixed(num, v, whole ? 0 : 1));
  }

  // value arc and end marker
  if (state == GaugeState::Ok || state == GaugeState::Warn) {
    const float a = ang(g.needle);
    if (a - a0 > 0.002F) {
      draw::arc_stroke(vg, cx, cy, radius, a0, a, 34.0F, hot ? draw::alpha(t.alarm, 0.20F) : t.glow, draw::Cap::Round);
      draw::arc_stroke(
          vg, cx, cy, radius, a0, a, 18.0F,
          draw::linear(vg, cx - radius, cy, cx + radius, cy, hot ? rgb(0xFF7A70) : t.fill0, hot ? warn_color : t.fill1),
          draw::Cap::Round);
    }
    const Point e = polar(cx, cy, a, radius);
    draw::circle_fill(
        vg, e.x, e.y, 32.0F,
        draw::radial(vg, e.x, e.y, 0.0F, 32.0F, hot ? draw::alpha(t.alarm, 0.55F) : t.halo, draw::alpha(t.halo, 0.0F)));
    draw::circle_fill(vg, e.x, e.y, 11.0F, hot ? warn_color : t.marker);
    draw::circle_fill(vg, e.x, e.y, 4.0F, t.bg);
  }

  // card title, top-left like every island (v6)
  draw::text(vg, f.ui_medium, 25.0F, t.ink3, draw::kLeftBaseline, 33.0F, 50.0F, spec.label, kTracking);

  // numeral, unit, caption
  std::string_view caption = "NORMAL";
  Color caption_color = t.ink3;
  switch (state) {
    case GaugeState::Stale:
      draw::text(vg, f.num_light, 120.0F, t.ink3, draw::kCenterMiddle, cx, cy - 6.0F, kDashes);
      draw::text(vg, f.ui_medium, 26.0F, t.ink3, draw::kCenterMiddle, cx, cy + 92.0F, "NO DATA", kTracking);
      caption = "TELEMETRY LOST";
      break;
    case GaugeState::SenderFault:
      draw::text(vg, f.num_light, 120.0F, t.alarm, draw::kCenterMiddle, cx, cy - 6.0F, kDashes);
      draw::text(vg, f.ui_medium, 26.0F, t.alarm, draw::kCenterMiddle, cx, cy + 92.0F, g.status.text, kTracking);
      caption = "CHECK SENDER WIRING";
      caption_color = t.alarm;
      break;
    case GaugeState::Ok:
    case GaugeState::Warn: {
      std::array<char, 16> buf{};
      draw::text(vg, f.num_light, 140.0F, hot ? warn_color : t.ink, draw::kCenterMiddle, cx, cy - 6.0F,
                 utils::format_fixed(buf, g.value, spec.decimals));
      draw::text(vg, f.ui_medium, 28.0F, t.ink2, draw::kCenterMiddle, cx, cy + 92.0F, spec.unit);
      if (hot) {
        caption = g.status.text;
        caption_color = warn_color;
      }
      break;
    }
  }
  draw::text(vg, f.ui_medium, 22.0F, caption_color, draw::kCenterMiddle, cx, 504.0F, caption, kTracking);
}

// ---------------------------------------------------------------------------
// bar gauge (fuel, volts) — mockup barGauge()
// ---------------------------------------------------------------------------

struct BarMark {
  float value;
  std::string_view label;
};

struct BarSpec {
  std::string_view label;
  std::string_view unit;
  float min;
  float max;
  int decimals;
  float zone_lo;  // danger band on the track; lo == hi disables
  float zone_hi;
  float band_lo;  // normal band; lo == hi disables
  float band_hi;
  std::array<BarMark, 5> marks;
  std::size_t mark_count;
};

void draw_bar_gauge(Canvas* vg, const Rect& r, const BarSpec& spec, const GaugeValue& g, const Theme& t,
                    const Fonts& f) noexcept {
  const GaugeState state = g.status.state;
  draw_tile(vg, r, t, state, g.severity);
  const draw::State st(vg);
  enter_tile_space(st, r, kBarRefH);
  if (state == GaugeState::Stale) {
    st.global_alpha(0.55F);
  }

  const float x0 = 36.0F;
  const float x1 = kTileRefW - 36.0F;
  const float bw = x1 - x0;
  const float by = 174.0F;  // v6: raised so the scale marks clear the card edge
  const float bh = 14.0F;
  const auto frac = [&](float v) noexcept { return normalized(v, spec.min, spec.max); };
  const bool hot = state == GaugeState::Warn;
  const Color warn_color = g.severity == Severity::Caution ? t.amber : t.alarm;

  // label and right-hand state caption
  draw::text(vg, f.ui_medium, 25.0F, t.ink3, draw::kLeftBaseline, 33.0F, 50.0F, spec.label, kTracking);
  switch (state) {
    case GaugeState::Stale:
      draw::text(vg, f.ui_medium, 22.0F, t.ink3, draw::kRightBaseline, x1 - 4.0F, 50.0F, "NO DATA", kTracking);
      break;
    case GaugeState::SenderFault:
      draw::text(vg, f.ui_medium, 22.0F, t.alarm, draw::kRightBaseline, x1 - 4.0F, 50.0F, g.status.text, kTracking);
      break;
    case GaugeState::Warn:
      draw::text(vg, f.ui_medium, 22.0F, warn_color, draw::kRightBaseline, x1 - 4.0F, 50.0F, g.status.text, kTracking);
      break;
    case GaugeState::Ok:
      break;
  }

  // numeral
  if (state == GaugeState::Stale || state == GaugeState::SenderFault) {
    draw::text(vg, f.num_light, 96.0F, state == GaugeState::SenderFault ? t.alarm : t.ink3, draw::kLeftBaseline, x0,
               146.0F, kDashes);
  } else {
    std::array<char, 16> buf{};
    const std::string_view numeral = utils::format_fixed(buf, g.value, spec.decimals);
    draw::text(vg, f.num_light, 96.0F, hot ? warn_color : t.ink, draw::kLeftBaseline, x0, 146.0F, numeral);
    const float w = draw::text_width(vg, f.num_light, 96.0F, numeral);
    draw::text(vg, f.ui_medium, 28.0F, t.ink2, draw::kLeftBaseline, x0 + w + 12.0F, 146.0F, spec.unit);
  }

  // track, normal band, danger zone
  draw::rounded_fill(vg, {.x = x0, .y = by, .w = bw, .h = bh}, 7.0F, t.track);
  if (state != GaugeState::Stale) {
    if (spec.band_hi > spec.band_lo) {
      const float a = x0 + bw * frac(spec.band_lo);
      const float b = x0 + bw * frac(spec.band_hi);
      draw::rounded_fill(vg, {.x = a, .y = by, .w = b - a, .h = bh}, 7.0F, t.band);
    }
    if (spec.zone_hi > spec.zone_lo) {
      const float a = x0 + bw * frac(spec.zone_lo);
      const float b = x0 + bw * frac(spec.zone_hi);
      draw::rounded_fill(vg, {.x = a, .y = by, .w = b - a, .h = bh}, 7.0F, t.zone);
    }
  }

  // fill and end marker
  if (state == GaugeState::Ok || state == GaugeState::Warn) {
    const float fw = std::max(bh, bw * frac(g.needle));
    draw::rounded_fill(vg, {.x = x0 - 6.0F, .y = by - 6.0F, .w = fw + 12.0F, .h = bh + 12.0F}, 13.0F, t.glow);
    const Color fill_start = hot ? (g.severity == Severity::Caution ? rgb(0xFFD98A) : rgb(0xFF7A70)) : t.fill0;
    draw::rounded_fill(vg, {.x = x0, .y = by, .w = fw, .h = bh}, 7.0F,
                       draw::linear(vg, x0, 0, x0 + bw, 0, fill_start, hot ? warn_color : t.fill1));
    const float ex = x0 + fw - bh / 2;
    const float ey = by + bh / 2;
    draw::circle_fill(vg, ex, ey, 10.0F, hot ? warn_color : t.marker);
    draw::circle_fill(vg, ex, ey, 3.5F, t.bg);
  }

  // scale marks
  for (std::size_t i = 0; i < spec.mark_count; ++i) {
    const BarMark& m = spec.marks[i];
    const float mx = x0 + bw * frac(m.value);
    draw::line(vg, mx, by + bh + 10.0F, mx, by + bh + 18.0F, 2.0F, t.ink3);
    draw::text(vg, f.num_medium, 22.0F, t.ink3, draw::kCenterBaseline, mx, by + bh + 46.0F, m.label);
  }
}

// ---------------------------------------------------------------------------
// specs. Oil and coolant follow the units setting (render/units.h); fuel and volts are fixed.
//
// zone_lo/hi paint the red band on the track. They mirror the MCU's documented thresholds
// (spec §4.1 faults) so the picture agrees with the fault, but nothing here decides a fault.
// ---------------------------------------------------------------------------

[[nodiscard]] constexpr ArcSpec arc_spec(std::string_view label, const UnitScale& u) noexcept {
  return {.label = label,
          .unit = u.unit,
          .min = u.min,
          .max = u.max,
          .major = u.major,
          .decimals = u.decimals,
          .zone_lo = u.zone_lo,
          .zone_hi = u.zone_hi};
}
constexpr BarSpec kFuelSpec{
    .label = "FUEL",
    .unit = "%",
    .min = 0.0F,
    .max = 100.0F,
    .decimals = 0,
    .zone_lo = 0.0F,
    .zone_hi = 8.0F,
    .band_lo = 0.0F,
    .band_hi = 0.0F,
    .marks = {{{0.0F, "E"}, {25.0F, "\xC2\xBC"}, {50.0F, "\xC2\xBD"}, {75.0F, "\xC2\xBE"}, {100.0F, "F"}}},
    .mark_count = 5};
[[nodiscard]] GaugeValue gauge_value(const SceneInput& in, Channel ch, float value, float needle,
                                     Severity severity) noexcept {
  return {.value = value,
          .needle = needle,
          .status = classify_gauge(in.telemetry.frame, in.telemetry.live(), ch),
          .severity = severity};
}

void draw_dot(Canvas* vg, float x, float y, Color c, bool glow) noexcept {
  if (glow) {
    draw::circle_fill(vg, x, y, 16.0F, draw::radial(vg, x, y, 4.0F, 16.0F, draw::alpha(c, 0.7F), draw::alpha(c, 0.0F)));
  }
  draw::circle_fill(vg, x, y, 7.0F, c);
}

}  // namespace

// ---------------------------------------------------------------------------
// classify_gauge — the only place fault codes become gauge states
// ---------------------------------------------------------------------------

GaugeStatus classify_gauge(const telemetry::Frame* frame, bool live, Channel ch) noexcept {
  if (!live || frame == nullptr) {
    return {.state = GaugeState::Stale, .text = "NO DATA"};
  }
  const auto has = [frame](FaultKind kind) noexcept {
    return std::any_of(frame->faults.begin(), frame->faults.end(),
                       [kind](const telemetry::Fault& x) noexcept { return x.kind == kind; });
  };
  for (const telemetry::Fault& x : frame->faults) {
    if (telemetry::has_channel(x.kind) && x.channel == ch) {
      return {.state = GaugeState::SenderFault,
              .text = x.kind == FaultKind::SenderShort ? "SENDER SHORT" : "SENDER OPEN"};
    }
  }
  switch (ch) {
    case Channel::Oil:
      if (has(FaultKind::OilLow)) {
        return {.state = GaugeState::Warn, .text = "LOW \xE2\x80\x94 STOP ENGINE"};
      }
      break;
    case Channel::Coolant:
      if (has(FaultKind::CltHigh)) {
        return {.state = GaugeState::Warn, .text = "OVERHEAT"};
      }
      break;
    case Channel::Fuel:
      if (has(FaultKind::FuelLow)) {
        return {.state = GaugeState::Warn, .text = "LOW FUEL"};
      }
      break;
    case Channel::Volt:
      if (has(FaultKind::VoltLow)) {
        return {.state = GaugeState::Warn, .text = "LOW \xC2\xB7 CHECK CHARGING"};
      }
      if (has(FaultKind::VoltHigh)) {
        return {.state = GaugeState::Warn, .text = "HIGH \xC2\xB7 CHECK CHARGING"};
      }
      break;
    case Channel::Count:
      break;
  }
  return {.state = GaugeState::Ok, .text = "NORMAL"};
}

// ---------------------------------------------------------------------------
// ConsoleScene
// ---------------------------------------------------------------------------

void ConsoleScene::Damped::step(float target, float rate, float dt_s) noexcept {
  if (!primed) {
    value = target;
    primed = true;
    return;
  }
  value += (target - value) * std::min(1.0F, rate * dt_s);
}

ConsoleScene::ConsoleScene(const Layout& layout, const Theme& theme, const Fonts& fonts) noexcept
    : layout_(layout),
      dock_(DockLayout::compute(layout.dock)),
      tabs_(TabsLayout::compute(layout.tabs)),
      tools_(ToolsLayout::compute(layout.status)),
      sheets_(compute_pages(layout)),
      camera_(CameraLayout::compute(layout.well)),
      theme_(theme),
      fonts_(fonts) {}

void ConsoleScene::update(const SceneInput& in) noexcept {
  if (in.telemetry.frame == nullptr) {
    return;
  }
  const telemetry::Frame& f = *in.telemetry.frame;
  oil_.step(f.oil_psi, 7.0F, in.dt_s);
  clt_.step(f.coolant_f, 7.0F, in.dt_s);
  fuel_percent_.step(f.fuel_fraction * 100.0F, 2.5F, in.dt_s);
}

void ConsoleScene::draw(Canvas* vg, const SceneInput& in) const noexcept {
  const Theme& t = theme_;
  const Fonts& f = fonts_;
  const auto panel_w = static_cast<float>(layout_.panel.width);
  const auto panel_h = static_cast<float>(layout_.panel.height);

  draw::rect_fill(vg, {.x = 0, .y = 0, .w = panel_w, .h = panel_h}, t.bg);

  const AlertList alerts = collect_alerts(in.telemetry.frame, in.telemetry.live(), in.telemetry.age_ms, in.units);
  draw_status_bar(vg, in, alerts);
  draw_status_tools(vg, tools_, t, in.well.tools);
  draw_tabs(vg, tabs_, t, f, in.well.tabs);

  const telemetry::Frame* fr = in.telemetry.frame;
  const PressureUnit pu = in.units.pressure;
  const TempUnit tu = in.units.temp;
  const float oil = to_display_pressure(pu, fr != nullptr ? fr->oil_psi : 0.0F);
  const float clt = to_display_temp(tu, fr != nullptr ? fr->coolant_f : 0.0F);
  const float fuel_percent = fr != nullptr ? fr->fuel_fraction * 100.0F : 0.0F;
  draw_arc_gauge(vg, layout_.oil, arc_spec("OIL PRESSURE", pressure_scale(pu)),
                 gauge_value(in, Channel::Oil, oil, to_display_pressure(pu, oil_.value), Severity::Alarm), t, f);
  draw_arc_gauge(vg, layout_.clt, arc_spec("COOLANT", temp_scale(tu)),
                 gauge_value(in, Channel::Coolant, clt, to_display_temp(tu, clt_.value), Severity::Alarm), t, f);
  draw_bar_gauge(vg, layout_.fuel, kFuelSpec,
                 gauge_value(in, Channel::Fuel, fuel_percent, fuel_percent_.value, Severity::Caution), t, f);
  draw_systems(vg, in);

  draw_well(vg, in);
  if (in.well.camera) {
    draw_camera(vg, camera_, t, f, in.well.camera_model);
  } else if (in.well.page != Page::None) {
    draw_sheet(vg, sheets_[static_cast<std::size_t>(in.well.page)], t, f, in.well.sheet_model);
  }
  draw_alerts(vg, layout_.well, t, f, alerts);
  draw_dock(vg, dock_, t, f, in.dock);
  draw_lever_hint(vg, layout_.lever, t, f);
}

void ConsoleScene::draw_status_bar(Canvas* vg, const SceneInput& in, const AlertList& alerts) const noexcept {
  const Theme& t = theme_;
  const Fonts& f = fonts_;
  const Rect& r = layout_.status;
  const float s = tools_.scale;  // 88-unit reference bar
  const float mid = r.cy();
  constexpr float kItemGap = 32.0F;        // between status items (mockup: gap 32)
  constexpr float kStatusTracking = 2.4F;  // .10em × 24

  // clock, left
  std::array<char, 8> hm{};
  const std::string_view clock = utils::format_into(hm, "%d:%02d", in.clock.hour12, in.clock.minute);
  draw::text(vg, f.num_medium, 48.0F * s, t.ink, draw::kLeftMiddle, r.x, mid, clock);
  const float clock_w = draw::text_width(vg, f.num_medium, 48.0F * s, clock);
  draw::text(vg, f.ui_medium, 22.0F * s, t.ink3, draw::kLeftBaseline, r.x + clock_w + 8.0F * s, mid + 16.0F * s,
             in.clock.pm ? "PM" : "AM", 1.8F * s);

  // the S mark, centred (mockup .brand), in the brand's own purple on both themes
  {
    const float h = 46.0F * s;
    const float w = brand_width(h);
    draw_brand_mark(vg, r.cx() - w / 2, mid - h / 2, h, rgba(0x9988CC, 0.95F));
  }

  // right side: IGN, volts; the theme button sits to the right of them
  float rx = tools_.left() - kItemGap * s;
  draw::text(vg, f.ui_medium, 24.0F * s, t.ink3, draw::kRightMiddle, rx, mid, "V", kStatusTracking * s);
  rx -= draw::text_width(vg, f.ui_medium, 24.0F * s, "V", kStatusTracking * s) + 10.0F * s;
  std::array<char, 8> vb{};
  const std::string_view volts = in.telemetry.live() && in.telemetry.frame != nullptr
                                     ? utils::format_fixed(vb, in.telemetry.frame->voltage_v, 1)
                                     : kDashes;
  draw::text(vg, f.num_medium, 30.0F * s, t.ink, draw::kRightMiddle, rx, mid, volts);
  rx -= draw::text_width(vg, f.num_medium, 30.0F * s, volts) + kItemGap * s;
  draw::text(vg, f.ui_medium, 24.0F * s, t.ink2, draw::kRightMiddle, rx, mid, "ON", kStatusTracking * s);
  rx -= draw::text_width(vg, f.ui_medium, 24.0F * s, "ON", kStatusTracking * s) + 12.0F * s;
  draw::text(vg, f.ui_medium, 24.0F * s, t.ink3, draw::kRightMiddle, rx, mid, "IGN", kStatusTracking * s);
  rx -= draw::text_width(vg, f.ui_medium, 24.0F * s, "IGN", kStatusTracking * s) + kItemGap * s;
  draw_alert_chip(vg, rx, mid, t, f, alerts);
}

// The Systems card (mockup v6 #tVolt.island): the links the spec defines, MCU (§4.3 stall)
// and CarPlay (dongle session), as outlined chips. Voltage itself lives in the status bar.
void ConsoleScene::draw_systems(Canvas* vg, const SceneInput& in) const noexcept {
  const Theme& t = theme_;
  const Fonts& f = fonts_;
  const Rect& r = layout_.systems;
  const float s = r.w / 496.0F;  // mockup card width
  constexpr float kPad = 24.0F;
  constexpr float kChipH = 64.0F;
  constexpr float kChipGap = 12.0F;

  draw::rounded_fill(vg, r, kTileRadius * s, t.surface);
  draw::rounded_stroke(vg, r, kTileRadius * s, t.line);
  draw::text(vg, f.ui_medium, 25.0F * s, t.ink3, draw::kLeftMiddle, r.x + (kPad + 8.0F) * s, r.y + (kPad + 14.0F) * s,
             "SYSTEMS", kTracking * s);

  struct Chip {
    std::string_view label;
    bool up;
    bool down;  // known-bad, as opposed to never seen
  };
  const bool live = in.telemetry.live();
  const bool mcu_bad = !live && in.telemetry.link != telemetry::LinkState::NoData;
  const std::array<Chip, 2> chips = {{{"MCU", live, mcu_bad}, {"CARPLAY", in.carplay_connected, false}}};
  const float chip_w = (r.w - 2 * (kPad + 4.0F) * s - kChipGap * s) / 2;
  const float chip_y = r.y + (kPad + 28.0F + 18.0F) * s;
  for (std::size_t i = 0; i < chips.size(); ++i) {
    const Rect chip{.x = r.x + (kPad + 4.0F) * s + static_cast<float>(i) * (chip_w + kChipGap * s),
                    .y = chip_y,
                    .w = chip_w,
                    .h = kChipH * s};
    draw::rounded_stroke(vg, chip, chip.h / 2, t.line2);
    const Color dot = chips[i].up ? t.ok : (chips[i].down ? t.alarm : t.ink3);
    draw_dot(vg, chip.x + 25.0F * s, chip.cy(), dot, chips[i].up || chips[i].down);
    draw::text(vg, f.ui_medium, 20.0F * s, t.ink2, draw::kLeftMiddle, chip.x + 44.0F * s, chip.cy(), chips[i].label,
               1.6F * s);
  }
}

void ConsoleScene::draw_well(Canvas* vg, const SceneInput& in) const noexcept {
  using std::numbers::pi_v;
  const Theme& t = theme_;
  const Fonts& f = fonts_;
  const Rect& r = layout_.well;
  constexpr float kWellRadius = 28.0F;
  // The well shows the phone's picture, which has its own appearance, so its chrome does not
  // follow our theme: fixed dark colours in both light and dark mode.
  const Color spinner_track = rgba(0xFFFFFF, 0.08F);
  const Color spinner = rgb(0x9A9EA6);
  const Color title_ink = rgb(0x9A9EA6);
  const Color subtitle_ink = rgb(0x5B6068);

  draw::rounded_fill(vg, r, kWellRadius, t.well_bg);
  draw::rounded_stroke(vg, r, kWellRadius, t.line2);

  if (in.carplay_connected) {
    return;  // the decoded video sits here
  }
  // connecting state: spinner ring, title, subtitle (mockup #cpwait)
  const float cx = r.cx();
  const float cy = r.cy() - 60.0F;
  draw::circle_stroke(vg, cx, cy, 48.0F, 5.0F, spinner_track);
  const float a = std::fmod(in.t_s * 2.0F * pi_v<float> / 1.1F, 2.0F * pi_v<float>);
  draw::arc_stroke(vg, cx, cy, 48.0F, a, a + pi_v<float> / 2.0F, 5.0F, spinner, draw::Cap::Round);
  draw::text(vg, f.ui_medium, 40.0F, title_ink, draw::kCenterMiddle, cx, cy + 110.0F, "Connecting to CarPlay");
  draw::text(vg, f.ui_medium, 26.0F, subtitle_ink, draw::kCenterMiddle, cx, cy + 164.0F, "DONGLE SESSION NEGOTIATING",
             1.6F);
}

}  // namespace c3::render
