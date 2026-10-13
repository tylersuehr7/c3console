#include "sheet.hpp"

#include "brand.hpp"
#include "draw.hpp"
#include "icons.hpp"
#include "telemetry/fault_codes.hpp"
#include "units.hpp"
#include "utils/fixed_format.hpp"
#include "widgets.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <span>
#include <string_view>
#include <utility>

namespace c3::render {
namespace {

// Reference geometry, mockup units (well 1520 wide, 1392 tall).
constexpr float kRefW = kReferenceContentW;
constexpr float kRadius = kWellRadius;
constexpr float kPadTop = 32.0F;
constexpr float kPadSide = 40.0F;
constexpr float kPadBottom = 40.0F;
constexpr float kHeadH = 72.0F;
constexpr float kHeadGap = 28.0F;
constexpr float kBackGap = 24.0F;
constexpr float kColGap = 24.0F;
constexpr float kBodyW = kRefW - 2 * kPadSide;   // 1440
constexpr float kColW = (kBodyW - kColGap) / 2;  // 708
constexpr float kCardPadTop = 28.0F;
constexpr float kCardPadSide = 32.0F;
constexpr float kCardPadBottom = 24.0F;
constexpr float kItemGap = 18.0F;
constexpr float kLabelH = 28.0F;  // .ctl-label
constexpr float kLabelGap = 8.0F;
constexpr float kControlH = 128.0F;
constexpr float kSliderLabelH = 36.0F;
constexpr float kSliderH = 72.0F;
constexpr float kEndsH = 20.0F;
constexpr float kSwatch = 84.0F;
constexpr float kSwatchGap = 20.0F;
constexpr std::size_t kSwatchesPerRow = 6;
constexpr float kKvRowH = 63.0F;
constexpr float kMuteW = 300.0F;
constexpr float kRowGap = 16.0F;
constexpr float kNavRowH = 176.0F;
constexpr float kNavGap = 16.0F;
constexpr float kNavIcon = 80.0F;
constexpr float kBannerH = 116.0F;
constexpr float kPoweredH = 44.0F;
constexpr float kNoteLine = 31.0F;  // one line of 22-px note text at 1.4 line height

constexpr float kLabelTracking = 3.4F;   // .14em × 24
constexpr float kSubTracking = 1.8F;     // .08em × 22
constexpr float kSliderTracking = 2.9F;  // .12em × 24
constexpr float kKvTracking = 1.9F;      // .08em × 24
constexpr float kEndsTracking = 2.0F;    // .10em × 20
constexpr float kNavSubTracking = 1.8F;  // .08em × 22

constexpr std::string_view kBuild = "c3console dev";
constexpr std::string_view kMiddleDot = " \xC2\xB7 ";

// Top-down placement inside a card's content area: take() hands out the next rect of the given
// height (in mockup units) and gap() skips space, so a page reads like the mockup's CSS flow.
struct Column {
  float x;
  float y;
  float w;
  float s;

  [[nodiscard]] Rect take(float h_units) noexcept {
    const Rect r{.x = x, .y = y, .w = w, .h = h_units * s};
    y += r.h;
    return r;
  }
  void gap(float units) noexcept { y += units * s; }
};

/// The content column of a card: inside its padding, flowing from the top.
[[nodiscard]] Column card_column(const Rect& card, float s) noexcept {
  return {.x = card.x + kCardPadSide * s, .y = card.y + kCardPadTop * s, .w = card.w - 2 * kCardPadSide * s, .s = s};
}

// A slider block: label row, then the track area (the control rect), optional end captions.
Rect slider_block(Column& col, bool ends) noexcept {
  col.gap(kSliderLabelH);
  const Rect track = col.take(kSliderH);
  if (ends) {
    col.gap(kEndsH);
  }
  return track;
}

// A labelled segmented group: label row, gap, container. Fills `out` with the buttons.
Rect segmented(Column& col, std::span<Rect> out) noexcept {
  col.gap(kLabelH + kLabelGap);
  const Rect group = col.take(kControlH);
  widgets::split_segments(group, col.s, out);
  return group;
}

[[nodiscard]] Rect& at(SheetLayout& sl, SheetControl k) noexcept {
  return sl.controls[static_cast<std::size_t>(k)];
}

// ---------------------------------------------------------------------------
// page geometry
// ---------------------------------------------------------------------------

void place_root(SheetLayout& sl, float gx, float gy) noexcept {
  const float s = sl.scale;

  constexpr std::array<SheetControl, 5> kRows = {SheetControl::RowAudio, SheetControl::RowTheme, SheetControl::RowUnits,
                                                 SheetControl::RowCalibration, SheetControl::RowSystem};

  for (std::size_t i = 0; i < kRows.size(); ++i) {
    at(sl, kRows[i]) = {
        .x = gx, .y = gy + static_cast<float>(i) * (kNavRowH + kNavGap) * s, .w = kBodyW * s, .h = kNavRowH * s};
  }
}

void place_audio(SheetLayout& sl, float gx, float gy, float grid_h) noexcept {
  const float s = sl.scale;
  const float col_w = kColW * s;
  const float right_x = gx + col_w + kColGap * s;

  // left column
  const Rect card_a{.x = gx, .y = gy, .w = col_w, .h = (kCardPadTop + kControlH + kCardPadBottom) * s};
  Column ca = card_column(card_a, s);
  const Rect row = ca.take(kControlH);
  const float slider_w = row.w - (kMuteW + kRowGap) * s;
  at(sl, SheetControl::Volume) = {.x = row.x,
                                  .y = row.y + (kControlH - kSliderLabelH - kSliderH) / 2 * s + kSliderLabelH * s,
                                  .w = slider_w,
                                  .h = kSliderH * s};
  at(sl, SheetControl::Mute) = {.x = row.right() - kMuteW * s, .y = row.y, .w = kMuteW * s, .h = row.h};

  const Rect card_b{
      .x = gx, .y = card_a.bottom() + kColGap * s, .w = col_w, .h = gy + grid_h - card_a.bottom() - kColGap * s};
  Column cb = card_column(card_b, s);
  at(sl, SheetControl::Balance) = slider_block(cb, true);
  cb.gap(kItemGap);
  at(sl, SheetControl::Fader) = slider_block(cb, true);
  cb.gap(kItemGap);
  at(sl, SheetControl::Sub) = slider_block(cb, true);

  // right column
  const Rect card_c{.x = right_x, .y = gy, .w = col_w, .h = grid_h};
  Column cc = card_column(card_c, s);
  at(sl, SheetControl::Bass) = slider_block(cc, false);
  cc.gap(kItemGap);
  at(sl, SheetControl::Mid) = slider_block(cc, false);
  cc.gap(kItemGap);
  at(sl, SheetControl::Treble) = slider_block(cc, false);
  cc.gap(kItemGap);
  sl.preset_group =
      segmented(cc, std::span<Rect>(sl.controls).subspan(static_cast<std::size_t>(SheetControl::PresetFlat), 4));

  sl.cards = {card_a, card_b, card_c, Rect{}, Rect{}, Rect{}};
  sl.card_count = 3;
}

void place_theme(SheetLayout& sl, float gx, float gy, float grid_h) noexcept {
  const float s = sl.scale;
  const float col_w = kColW * s;
  const float right_x = gx + col_w + kColGap * s;

  // left: theme card, preview card
  const Rect card_a{
      .x = gx,
      .y = gy,
      .w = col_w,
      .h = (kCardPadTop + kLabelH + kLabelGap + kControlH + kItemGap + 3 * kNoteLine + kCardPadBottom) * s};
  Column ca = card_column(card_a, s);
  sl.theme_group =
      segmented(ca, std::span<Rect>(sl.controls).subspan(static_cast<std::size_t>(SheetControl::ThemeDark), 3));

  const Rect card_b{
      .x = gx, .y = card_a.bottom() + kColGap * s, .w = col_w, .h = gy + grid_h - card_a.bottom() - kColGap * s};
  Column cb = card_column(card_b, s);
  cb.gap(kLabelH + kItemGap);
  sl.preview = cb.take(96.0F);

  // right: accent card
  const Rect card_c{.x = right_x, .y = gy, .w = col_w, .h = grid_h};
  Column cc = card_column(card_c, s);
  cc.gap(kLabelH + kItemGap);
  cc.gap(6.0F);  // .swatches padding
  for (std::size_t i = 0; i < kSwatches.size(); ++i) {
    const std::size_t row = i / kSwatchesPerRow;
    const std::size_t col = i % kSwatchesPerRow;
    at(sl, static_cast<SheetControl>(static_cast<std::size_t>(SheetControl::Swatch0) + i)) = {
        .x = cc.x + (4.0F + static_cast<float>(col) * (kSwatch + kSwatchGap)) * s,
        .y = cc.y + static_cast<float>(row) * (kSwatch + kSwatchGap) * s,
        .w = kSwatch * s,
        .h = kSwatch * s};
  }
  cc.gap(2 * kSwatch + kSwatchGap + 6.0F);
  cc.gap(kItemGap);
  at(sl, SheetControl::Hue) = slider_block(cc, false);

  sl.cards = {card_a, card_b, card_c, Rect{}, Rect{}, Rect{}};
  sl.card_count = 3;
}

void place_units(SheetLayout& sl, float gx, float gy, float grid_h) noexcept {
  const float s = sl.scale;
  const float col_w = kColW * s;
  const Rect card_a{.x = gx, .y = gy, .w = col_w, .h = grid_h};
  const Rect card_b{.x = gx + col_w + kColGap * s, .y = gy, .w = col_w, .h = grid_h};
  Column ca = card_column(card_a, s);
  sl.temp_group = segmented(ca, std::span<Rect>(sl.controls).subspan(static_cast<std::size_t>(SheetControl::TempF), 2));
  Column cb = card_column(card_b, s);
  sl.press_group =
      segmented(cb, std::span<Rect>(sl.controls).subspan(static_cast<std::size_t>(SheetControl::PressPsi), 2));
  sl.cards = {card_a, card_b, Rect{}, Rect{}, Rect{}, Rect{}};
  sl.card_count = 2;
}

void place_calibration(SheetLayout& sl, float gx, float gy, float grid_h) noexcept {
  const float s = sl.scale;
  sl.banner = {.x = gx, .y = gy, .w = kBodyW * s, .h = kBannerH * s};
  const float top = sl.banner.bottom() + kColGap * s;
  const float col_w = kColW * s;
  const float row_h = (gy + grid_h - top - kColGap * s) / 2;
  for (std::size_t i = 0; i < telemetry::kChannelCount; ++i) {
    const std::size_t col = i % 2;
    const std::size_t row = i / 2;
    sl.cards[i] = {.x = gx + static_cast<float>(col) * (col_w + kColGap * s),
                   .y = top + static_cast<float>(row) * (row_h + kColGap * s),
                   .w = col_w,
                   .h = row_h};
  }
  sl.card_count = telemetry::kChannelCount;
}

void place_system(SheetLayout& sl, float gx, float gy, float grid_h) noexcept {
  const float s = sl.scale;
  const float col_w = kColW * s;
  const float cards_h = grid_h - (kPoweredH + kColGap) * s;
  const Rect card_a{.x = gx, .y = gy, .w = col_w, .h = cards_h};
  const Rect card_b{.x = gx + col_w + kColGap * s, .y = gy, .w = col_w, .h = cards_h};
  Column ca = card_column(card_a, s);
  ca.gap(kLabelH + kItemGap);
  sl.kv = ca.take(4 * kKvRowH);
  Column cb = card_column(card_b, s);
  cb.gap(kLabelH + kItemGap);
  sl.kv2 = cb.take(3 * kKvRowH);
  at(sl, SheetControl::Reset) = {
      .x = cb.x, .y = card_b.bottom() - (kCardPadBottom + kControlH) * s, .w = cb.w, .h = kControlH * s};
  sl.powered = {.x = gx, .y = card_a.bottom() + kColGap * s, .w = kBodyW * s, .h = kPoweredH * s};
  sl.cards = {card_a, card_b, Rect{}, Rect{}, Rect{}, Rect{}};
  sl.card_count = 2;
}

[[nodiscard]] constexpr std::string_view link_text(telemetry::LinkState s) noexcept {
  switch (s) {
    case telemetry::LinkState::Live:
      return "10 Hz \xC2\xB7 ok";
    case telemetry::LinkState::Stalled:
      return "stalled";
    case telemetry::LinkState::Failed:
      return "failed";
    case telemetry::LinkState::NoData:
      return "no data";
  }
  return "?";
}

struct PageText {
  std::string_view title;
  std::string_view sub;
};

[[nodiscard]] constexpr PageText page_text(Page p) noexcept {
  switch (p) {
    case Page::Root:
      return {"Settings", "STORED ON THE CONSOLE \xC2\xB7 WRITABLE DATA PARTITION, NOT THE PHONE"};
    case Page::Audio:
      return {"Audio", "PROCESSING ON PI \xC2\xB7 SPDIF TO DSP AMP \xC2\xB7 KNOB IS THE PRIMARY VOLUME CONTROL"};
    case Page::Theme:
      return {"Theme", "APPEARANCE AND ACCENT \xC2\xB7 AUTO FOLLOWS THE HEADLAMP RELAY"};
    case Page::Units:
      return {"Units", "GAUGE SCALES, REDLINES AND BANNERS FOLLOW"};
    case Page::Calibration:
      return {"Calibration",
              "SPEC \xC2\xA7"
              "9.2 \xC2\xB7 RAW ADC COUNTS BESIDE TABLE AND MCU VALUES"};
    case Page::System:
      return {"System", "CONSOLE, SENSOR BOARD AND BUILD"};
    case Page::None:
    case Page::Count:
      break;
  }
  return {"", ""};
}

// Joins parts into `buf`, upper-casing ASCII letters (UTF-8 bytes pass through). Fixed buffer.
[[nodiscard]] std::string_view join_upper(std::span<char> buf, std::initializer_list<std::string_view> parts) noexcept {
  std::size_t n = 0;
  for (const std::string_view part : parts) {
    for (const char c : part) {
      if (n + 1 >= buf.size()) {
        break;
      }
      buf[n++] = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    }
  }
  if (!buf.empty()) {
    buf[std::min(n, buf.size() - 1)] = '\0';
  }
  return {buf.data(), n};
}

// ---------------------------------------------------------------------------
// drawing helpers
// ---------------------------------------------------------------------------

// What every page drawer needs: the canvas, theme, fonts and the sheet's scale.
struct Ctx {
  Canvas* vg;
  const Theme& t;
  const Fonts& f;
  float s;
};

void card(const Ctx& c, const Rect& r) noexcept {
  draw::rounded_fill(c.vg, r, widgets::kTileRadius * c.s, c.t.surface);
  draw::rounded_stroke(c.vg, r, widgets::kTileRadius * c.s, c.t.line);
}

// .ctl-label above a control rect, with an optional dimmer hint after it.
void control_label(const Ctx& c, const Rect& control, std::string_view text, std::string_view hint = {}) noexcept {
  const float x = control.x + 8.0F * c.s;
  const float y = control.y - (kLabelGap + kLabelH / 2) * c.s;
  draw::text(c.vg, c.f.ui_medium, 24.0F * c.s, c.t.ink3, draw::kLeftMiddle, x, y, text, kLabelTracking * c.s);
  if (!hint.empty()) {
    const float tw = draw::text_width(c.vg, c.f.ui_medium, 24.0F * c.s, text, kLabelTracking * c.s);
    draw::text(c.vg, c.f.ui_medium, 24.0F * c.s, draw::alpha(c.t.ink3, 0.7F), draw::kLeftMiddle, x + tw + 16.0F * c.s,
               y, hint);
  }
}

// Card title at the card's top-left, the way islands carry theirs.
void card_label(const Ctx& c, const Rect& card_r, std::string_view text, std::string_view hint = {}) noexcept {
  const Rect anchor{.x = card_r.x + kCardPadSide * c.s,
                    .y = card_r.y + (kCardPadTop + kLabelH + kLabelGap) * c.s,
                    .w = card_r.w - 2 * kCardPadSide * c.s,
                    .h = 0};
  control_label(c, anchor, text, hint);
}

// .sl: label left, numeral right (with a small unit), then the track in `track`.
void slider(const Ctx& c, const Rect& track, std::string_view label, std::string_view value, std::string_view unit,
            float frac, const widgets::SliderStyle& style, std::string_view end_lo = {},
            std::string_view end_hi = {}) noexcept {
  const float ly = track.y - kSliderLabelH / 2 * c.s;
  draw::text(c.vg, c.f.ui_medium, 24.0F * c.s, c.t.ink3, draw::kLeftMiddle, track.x, ly, label, kSliderTracking * c.s);
  float rx = track.right();
  if (!unit.empty()) {
    draw::text(c.vg, c.f.ui_regular, 22.0F * c.s, c.t.ink3, draw::kRightMiddle, rx, ly, unit);
    rx -= draw::text_width(c.vg, c.f.ui_regular, 22.0F * c.s, unit) + 6.0F * c.s;
  }
  draw::text(c.vg, c.f.num_medium, 36.0F * c.s, c.t.ink, draw::kRightMiddle, rx, ly, value);
  widgets::slider(c.vg, c.t, c.s, track, frac, style, c.t.sheet);
  if (!end_lo.empty()) {
    const float ey = track.bottom() + kEndsH / 2 * c.s;
    draw::text(c.vg, c.f.ui_medium, 20.0F * c.s, c.t.ink3, draw::kLeftMiddle, track.x, ey, end_lo, kEndsTracking * c.s);
    draw::text(c.vg, c.f.ui_medium, 20.0F * c.s, c.t.ink3, draw::kRightMiddle, track.right(), ey, end_hi,
               kEndsTracking * c.s);
  }
}

void segment_group(const Ctx& c, const Rect& group) noexcept {
  draw::rounded_fill(c.vg, group, widgets::kControlRadius * c.s, c.t.surface);
  draw::rounded_stroke(c.vg, group, widgets::kControlRadius * c.s, c.t.line);
}

template <std::size_t N>
void segments(const Ctx& c, const SheetLayout& sl, const SheetModel& m, const Rect& group, SheetControl first,
              const std::array<std::string_view, N>& captions, std::size_t on_index) noexcept {
  segment_group(c, group);
  for (std::size_t i = 0; i < N; ++i) {
    const auto k = static_cast<SheetControl>(static_cast<std::size_t>(first) + i);
    widgets::segment_button(c.vg, c.f, c.t, c.s, sl.rect(k), captions[i], on_index == i, m.pressed == k);
  }
}

void note(const Ctx& c, float x, float y, float w, std::string_view text, Color colour) noexcept {
  draw::text_box(c.vg, c.f.ui_regular, 22.0F * c.s, colour, x, y, w, 1.4F, text);
}
void note(const Ctx& c, float x, float y, float w, std::string_view text) noexcept {
  note(c, x, y, w, text, c.t.ink3);
}
void card_note(const Ctx& c, const Rect& card_r, float y, std::string_view text) noexcept {
  note(c, card_r.x + kCardPadSide * c.s, y, card_r.w - 2 * kCardPadSide * c.s, text);
}

void kv_rows(const Ctx& c, const Rect& kv,
             std::span<const std::pair<std::string_view, std::string_view>> rows) noexcept {
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const float ry = kv.y + (static_cast<float>(i) + 0.5F) * kKvRowH * c.s;
    draw::text(c.vg, c.f.ui_medium, 24.0F * c.s, c.t.ink3, draw::kLeftMiddle, kv.x, ry, rows[i].first,
               kKvTracking * c.s);
    draw::text(c.vg, c.f.num_medium, 30.0F * c.s, c.t.ink, draw::kRightMiddle, kv.right(), ry, rows[i].second);
    draw::rect_fill(c.vg,
                    {.x = kv.x, .y = kv.y + static_cast<float>(i + 1) * kKvRowH * c.s - 1.0F, .w = kv.w, .h = 1.0F},
                    c.t.line);
  }
}

void head(const Ctx& c, const SheetLayout& sl, const SheetModel& m) noexcept {
  const PageText text = page_text(sl.kind);
  float x = sl.frame.x + kPadSide * c.s;
  const float y = sl.frame.y + kPadTop * c.s;
  if (sl.has(SheetControl::Back)) {
    widgets::icon_button(c.vg, c.t, sl.rect(SheetControl::Back), Icon::Back, false, m.pressed == SheetControl::Back);
    x += (kHeadH + kBackGap) * c.s;
  }
  draw::text(c.vg, c.f.ui_semibold, 40.0F * c.s, c.t.ink, draw::kLeftTop, x, y, text.title);
  draw::text(c.vg, c.f.ui_medium, 22.0F * c.s, c.t.ink3, draw::kLeftTop, x, y + 48.0F * c.s, text.sub,
             kSubTracking * c.s);
  widgets::icon_button(c.vg, c.t, sl.rect(SheetControl::Close), Icon::Close, false, m.pressed == SheetControl::Close);
}

[[nodiscard]] float frac_of(int v, int lo, int hi) noexcept {
  return static_cast<float>(v - lo) / static_cast<float>(hi - lo);
}

template <std::size_t N>
[[nodiscard]] std::string_view signed_int(std::array<char, N>& buf, int v) noexcept {
  return utils::format_into(buf, "%+d", v);
}

[[nodiscard]] bool placeholder_table(const SheetModel& m) noexcept {
  return m.calibration == nullptr || m.calibration->placeholder;
}

// ---------------------------------------------------------------------------
// pages
// ---------------------------------------------------------------------------

void nav_row(const Ctx& c, const SheetLayout& sl, const SheetModel& m, SheetControl k, Icon icon,
             std::string_view title, std::string_view sub) noexcept {
  const Rect& r = sl.rect(k);
  const float s = c.s;
  draw::rounded_fill(c.vg, r, widgets::kTileRadius * s, m.pressed == k ? c.t.surface2 : c.t.surface);
  draw::rounded_stroke(c.vg, r, widgets::kTileRadius * s, c.t.line);
  const float ix = r.x + 36.0F * s + kNavIcon * s / 2;
  draw::circle_fill(c.vg, ix, r.cy(), kNavIcon * s / 2, c.t.surface2);
  draw::circle_hairline(c.vg, ix, r.cy(), kNavIcon * s / 2, c.t.line);
  draw_icon(c.vg, icon, ix, r.cy(), 36.0F * s, c.t.ink2);
  const float tx = r.x + (36.0F + kNavIcon + 28.0F) * s;
  draw::text(c.vg, c.f.ui_semibold, 34.0F * s, c.t.ink, draw::kLeftBaseline, tx, r.cy() - 6.0F * s, title);
  draw::text(c.vg, c.f.ui_medium, 22.0F * s, c.t.ink3, draw::kLeftBaseline, tx, r.cy() + 32.0F * s, sub,
             kNavSubTracking * s);
  draw_icon(c.vg, Icon::ChevronRight, r.right() - 48.0F * s, r.cy(), 32.0F * s, c.t.ink3, 2.2F);
}

void draw_root(const Ctx& c, const SheetLayout& sl, const SheetModel& m) noexcept {
  std::array<char, 40> a{};
  std::array<char, 16> vol{};
  const std::string_view volume =
      m.audio.mute ? std::string_view("MUTED") : utils::format_into(vol, "VOLUME %d", m.audio.volume);
  nav_row(c, sl, m, SheetControl::RowAudio, Icon::Speaker, "Audio",
          join_upper(a, {volume, kMiddleDot, to_string(m.audio.preset)}));

  std::array<char, 40> b{};
  const std::optional<std::size_t> sw = swatch_index(m.settings.accent);
  nav_row(
      c, sl, m, SheetControl::RowTheme, Icon::Contrast, "Theme",
      join_upper(b, {to_string(m.settings.theme), m.settings.theme == ThemePreference::Auto ? " \xC2\xB7 LAMPS" : "",
                     kMiddleDot, sw ? kSwatches[*sw].name : "Custom"}));

  std::array<char, 24> u{};
  nav_row(c, sl, m, SheetControl::RowUnits, Icon::Calibrate, "Units",
          join_upper(
              u, {temp_scale(m.settings.units.temp).unit, kMiddleDot, pressure_scale(m.settings.units.pressure).unit}));

  nav_row(c, sl, m, SheetControl::RowCalibration, Icon::Target, "Calibration",
          placeholder_table(m) ? "PLACEHOLDER TABLE \xC2\xB7 SPEC \xC2\xA7"
                                 "9.2"
                               : "MEASURED TABLE \xC2\xB7 BENCH CALIBRATED");

  std::array<char, 32> sys{};
  nav_row(c, sl, m, SheetControl::RowSystem, Icon::Monitor, "System", join_upper(sys, {"MCU ", link_text(m.link)}));
}

void draw_audio(const Ctx& c, const SheetLayout& sl, const SheetModel& m) noexcept {
  const audio::AudioState& a = m.audio;
  for (std::size_t i = 0; i < sl.card_count; ++i) {
    card(c, sl.cards[i]);
  }
  std::array<char, 8> buf{};
  const widgets::SliderStyle big{.track_h = 20.0F, .thumb_d = 60.0F, .fill_from_start = true};
  const widgets::SliderStyle centred{.centre_tick = true};

  slider(c, sl.rect(SheetControl::Volume), "VOLUME", utils::format_into(buf, "%d", a.volume), {},
         frac_of(a.volume, 0, 100), big);
  const Icon mute_icon = Icon::SpeakerMuted;
  widgets::toggle(c.vg, c.f, c.t, c.s, sl.rect(SheetControl::Mute), "Mute", a.mute ? "ON" : "OFF", a.mute,
                  m.pressed == SheetControl::Mute, &mute_icon);

  slider(c, sl.rect(SheetControl::Balance), "BALANCE", signed_int(buf, a.balance), {}, frac_of(a.balance, -10, 10),
         centred, "LEFT", "RIGHT");
  slider(c, sl.rect(SheetControl::Fader), "FADER", signed_int(buf, a.fader), {}, frac_of(a.fader, -10, 10), centred,
         "REAR", "FRONT");
  slider(c, sl.rect(SheetControl::Sub), "SUB LEVEL", signed_int(buf, a.sub_db), "dB", frac_of(a.sub_db, -12, 6),
         centred,
         "\xE2\x88\x92"
         "12",
         "+6");
  slider(c, sl.rect(SheetControl::Bass), "BASS", signed_int(buf, a.bass_db), "dB", frac_of(a.bass_db, -6, 6), centred);
  slider(c, sl.rect(SheetControl::Mid), "MID", signed_int(buf, a.mid_db), "dB", frac_of(a.mid_db, -6, 6), centred);
  slider(c, sl.rect(SheetControl::Treble), "TREBLE", signed_int(buf, a.treble_db), "dB", frac_of(a.treble_db, -6, 6),
         centred);

  control_label(c, sl.preset_group, "PRESET");
  constexpr std::array<std::string_view, 4> kPresets = {"Flat", "Rock", "Cruise", "Voice"};
  segments(c, sl, m, sl.preset_group, SheetControl::PresetFlat, kPresets, static_cast<std::size_t>(a.preset));
  card_note(c, sl.cards[2], sl.preset_group.bottom() + kItemGap * c.s,
            "Tone and level shaping run as a Pi-side DSP stage before the SPDIF output. Crossovers, "
            "time alignment and room EQ stay in the amplifier's own tuning software.");
}

void draw_preview(const Ctx& c, const Rect& p) noexcept {
  const float seg_w = (p.w - (240.0F + 2 * 16.0F) * c.s) * 0.5F;
  const Rect seg{.x = p.x, .y = p.y, .w = seg_w, .h = p.h};
  segment_group(c, seg);
  std::array<Rect, 3> buttons{};
  widgets::split_segments(seg, c.s, buttons);
  constexpr std::array<std::string_view, 3> kPreviewCaptions = {"Off", "1", "2"};
  for (std::size_t i = 0; i < buttons.size(); ++i) {
    widgets::segment_button(c.vg, c.f, c.t, c.s * 0.8F, buttons[i], kPreviewCaptions[i], i == 1, false);
  }
  const Rect tog{.x = seg.right() + 16.0F * c.s, .y = p.y, .w = 240.0F * c.s, .h = p.h};
  const Icon ac = Icon::Snowflake;
  widgets::toggle(c.vg, c.f, c.t, c.s * 0.8F, tog, "A/C", "PREVIEW", true, false, &ac);
  const Rect bar{.x = tog.right() + 16.0F * c.s,
                 .y = p.cy() - 6.0F * c.s,
                 .w = p.right() - tog.right() - 16.0F * c.s,
                 .h = 12.0F * c.s};
  draw::rounded_fill(c.vg, bar, bar.h / 2, c.t.line2);
  draw::rounded_fill(c.vg, {.x = bar.x, .y = bar.y, .w = bar.w * 0.62F, .h = bar.h}, bar.h / 2, c.t.ember);
}

void draw_theme(const Ctx& c, const SheetLayout& sl, const SheetModel& m) noexcept {
  const Settings& st = m.settings;
  for (std::size_t i = 0; i < sl.card_count; ++i) {
    card(c, sl.cards[i]);
  }
  // theme
  control_label(c, sl.theme_group, "THEME");
  constexpr std::array<std::string_view, 3> kThemeCaptions = {"Dark", "Light", "Auto \xC2\xB7 lamps"};
  segments(c, sl, m, sl.theme_group, SheetControl::ThemeDark, kThemeCaptions, static_cast<std::size_t>(st.theme));
  card_note(c, sl.cards[0], sl.theme_group.bottom() + kItemGap * c.s,
            "Auto follows the headlamp relay: light by day, dark once the lamps are on. The "
            "sun/moon button in the status bar sets an explicit theme and leaves Auto.");

  // preview
  card_label(c, sl.cards[1], "PREVIEW");
  draw_preview(c, sl.preview);
  card_note(c, sl.cards[1], sl.preview.bottom() + kItemGap * c.s,
            "How selected controls, toggles and level bars look with this accent. Nothing here "
            "is live.");

  // accent
  std::array<char, 12> hex{};
  card_label(c, sl.cards[2], "ACCENT", utils::format_into(hex, "#%06X", st.accent));
  const std::optional<std::size_t> current = swatch_index(st.accent);
  for (std::size_t i = 0; i < kSwatches.size(); ++i) {
    const auto k = static_cast<SheetControl>(static_cast<std::size_t>(SheetControl::Swatch0) + i);
    widgets::swatch(c.vg, c.t, sl.rect(k), rgb(kSwatches[i].rgb), current == i);
  }
  const float hue = hue_of(st.accent);
  std::array<char, 8> deg{};
  const Rect& hue_r = sl.rect(SheetControl::Hue);
  const float ly = hue_r.y - kSliderLabelH / 2 * c.s;
  draw::text(c.vg, c.f.ui_medium, 24.0F * c.s, c.t.ink3, draw::kLeftMiddle, hue_r.x, ly, "CUSTOM HUE",
             kSliderTracking * c.s);
  draw::text(c.vg, c.f.num_medium, 36.0F * c.s, c.t.ink, draw::kRightMiddle, hue_r.right(), ly,
             utils::format_into(deg, "%.0f\xC2\xB0", static_cast<double>(hue)));
  widgets::hue_track(c.vg, c.s, hue_r, hue / 360.0F, c.t.ember, c.t.sheet);
  card_note(c, sl.cards[2], hue_r.bottom() + kItemGap * c.s,
            "The accent marks what is selected or interactive. Warnings stay red and amber and "
            "gauges stay monochrome whatever you choose.");
}

void draw_units(const Ctx& c, const SheetLayout& sl, const SheetModel& m) noexcept {
  for (std::size_t i = 0; i < sl.card_count; ++i) {
    card(c, sl.cards[i]);
  }
  control_label(c, sl.temp_group, "TEMPERATURE");
  constexpr std::array<std::string_view, 2> kTempCaptions = {
      "\xC2\xB0"
      "F",
      "\xC2\xB0"
      "C"};
  segments(c, sl, m, sl.temp_group, SheetControl::TempF, kTempCaptions,
           static_cast<std::size_t>(m.settings.units.temp));
  card_note(c, sl.cards[0], sl.temp_group.bottom() + kItemGap * c.s,
            "Coolant arc rescales to 100\xE2\x80\x93"
            "260 \xC2\xB0"
            "F with the redline at 235, or 40\xE2\x80\x93"
            "120 \xC2\xB0"
            "C with the redline at 113. Alert banners follow.");

  control_label(c, sl.press_group, "PRESSURE");
  constexpr std::array<std::string_view, 2> kPressCaptions = {"psi", "bar"};
  segments(c, sl, m, sl.press_group, SheetControl::PressPsi, kPressCaptions,
           static_cast<std::size_t>(m.settings.units.pressure));
  card_note(c, sl.cards[1], sl.press_group.bottom() + kItemGap * c.s,
            "Oil arc rescales to 0\xE2\x80\x93"
            "80 psi with the low zone 0\xE2\x80\x93"
            "12, or 0\xE2\x80\x93"
            "6 bar with the low zone 0\xE2\x80\x93"
            "0.8. Alert banners follow.");
}

// One calibration card: channel name, point count, then RAW / TABLE / MCU numerals.
void calibration_card(const Ctx& c, const Rect& r, const SheetModel& m, telemetry::Channel ch) noexcept {
  constexpr std::array<std::string_view, telemetry::kChannelCount> kNames = {"FUEL", "OIL PRESSURE", "COOLANT",
                                                                             "SYSTEM VOLTAGE"};
  constexpr std::array<std::string_view, telemetry::kChannelCount> kUnits = {"fraction", "psi",
                                                                             "\xC2\xB0"
                                                                             "F",
                                                                             "V"};
  constexpr std::array<int, telemetry::kChannelCount> kDecimals = {3, 1, 1, 2};
  const auto i = static_cast<std::size_t>(ch);
  const float s = c.s;
  card(c, r);
  std::array<char, 32> pts{};
  const bool placeholder = placeholder_table(m);
  const std::size_t n = m.calibration != nullptr ? m.calibration->curve(ch).size() : 0;
  card_label(c, r, kNames[i],
             utils::format_into(pts, "%zu points \xC2\xB7 %s", n, placeholder ? "placeholder" : "measured"));

  std::string_view raw = "\xE2\x80\x94";
  std::string_view table = raw;
  std::string_view mcu = raw;
  std::array<char, 16> rbuf{};
  std::array<char, 16> tbuf{};
  std::array<char, 16> mbuf{};
  if (m.live && m.frame != nullptr) {
    const std::int32_t count = m.frame->raw[i];
    raw = utils::format_into(rbuf, "%d", count);
    if (m.calibration != nullptr) {
      table = utils::format_fixed(tbuf, static_cast<double>(m.calibration->convert(ch, count)), kDecimals[i]);
    }
    const float mcu_value = ch == telemetry::Channel::Fuel      ? m.frame->fuel_fraction
                            : ch == telemetry::Channel::Oil     ? m.frame->oil_psi
                            : ch == telemetry::Channel::Coolant ? m.frame->coolant_f
                                                                : m.frame->voltage_v;
    mcu = utils::format_fixed(mbuf, static_cast<double>(mcu_value), kDecimals[i]);
  }
  const float ix = r.x + (kCardPadSide + 8.0F) * s;
  const float iw = r.w - 2 * (kCardPadSide + 8.0F) * s;
  const float col_w = iw / 3;
  const float cap_y = r.cy() - 26.0F * s;
  const float num_y = r.cy() + 48.0F * s;
  const std::array<std::pair<std::string_view, std::string_view>, 3> cols = {
      {{"RAW COUNT", raw}, {"TABLE", table}, {"MCU", mcu}}};
  for (std::size_t k = 0; k < cols.size(); ++k) {
    const float x = ix + static_cast<float>(k) * col_w;
    draw::text(c.vg, c.f.ui_medium, 21.0F * s, c.t.ink3, draw::kLeftMiddle, x, cap_y, cols[k].first, kSubTracking * s);
    draw::text(c.vg, c.f.num_light, 64.0F * s, k == 1 && placeholder ? c.t.ink3 : c.t.ink, draw::kLeftBaseline, x,
               num_y, cols[k].second);
  }
  draw::text(c.vg, c.f.ui_medium, 22.0F * s, c.t.ink2, draw::kLeftBaseline, ix + 2 * col_w, num_y + 30.0F * s,
             kUnits[i]);
}

void draw_calibration(const Ctx& c, const SheetLayout& sl, const SheetModel& m) noexcept {
  // Safety invariant 5: say what the table is before showing any number from it.
  const bool placeholder = placeholder_table(m);
  const Rect& b = sl.banner;
  draw::rounded_fill(c.vg, b, widgets::kControlRadius * c.s,
                     placeholder ? draw::alpha(c.t.amber, 0.14F) : c.t.surface2);
  draw::rounded_stroke(c.vg, b, widgets::kControlRadius * c.s, placeholder ? draw::alpha(c.t.amber, 0.45F) : c.t.line2);
  const float bx = b.x + 32.0F * c.s;
  const float by = b.y + 22.0F * c.s;
  const std::string_view title = placeholder ? "PLACEHOLDER TABLE" : "MEASURED TABLE";
  draw::text(c.vg, c.f.ui_semibold, 22.0F * c.s, placeholder ? c.t.amber : c.t.ink, draw::kLeftTop, bx, by, title,
             kSubTracking * c.s);
  const float tw = draw::text_width(c.vg, c.f.ui_semibold, 22.0F * c.s, title, kSubTracking * c.s);
  note(c, bx + tw + 12.0F * c.s, by - 1.0F * c.s, b.w - tw - 76.0F * c.s,
       placeholder ? "\xE2\x80\x94 identity mapping from config/calibration.json; nothing here is "
                     "calibrated. TABLE is this console's file, MCU the firmware's own conversion. "
                     "Point capture arrives with the cal command channel (spec \xC2\xA7"
                     "9.2)."
                   : "\xE2\x80\x94 bench measured. TABLE is this console's file; MCU is the firmware's "
                     "own conversion.",
       c.t.ink2);
  for (std::size_t i = 0; i < telemetry::kChannelCount; ++i) {
    calibration_card(c, sl.cards[i], m, static_cast<telemetry::Channel>(i));
  }
}

void draw_system(const Ctx& c, const SheetLayout& sl, const SheetModel& m) noexcept {
  for (std::size_t i = 0; i < sl.card_count; ++i) {
    card(c, sl.cards[i]);
  }
  card_label(c, sl.cards[0], "CONSOLE");
  std::array<char, 24> panel_val{};
  std::array<char, 24> well_val{};
  const std::array<std::pair<std::string_view, std::string_view>, 4> console_rows = {{
      {"PANEL", utils::format_into(panel_val, "%d \xC3\x97 %d", sl.panel.width, sl.panel.height)},
      {"CARPLAY WELL", utils::format_into(well_val, "%.0f \xC3\x97 %.0f", static_cast<double>(sl.well.w),
                                          static_cast<double>(sl.well.h))},
      {"CONSOLE BUILD", kBuild},
      {"RENDERER", "NanoVG"},
  }};
  kv_rows(c, sl.kv, console_rows);

  card_label(c, sl.cards[1], "SENSOR BOARD");
  const std::array<std::pair<std::string_view, std::string_view>, 3> sensor_rows = {{
      {"MCU LINK", link_text(m.link)},
      {"FIRMWARE", "c3fw \xC2\xB7 ESP32-S3"},
      {"CALIBRATION", placeholder_table(m) ? "placeholder" : "measured"},
  }};
  kv_rows(c, sl.kv2, sensor_rows);
  const Icon reset_icon = Icon::Reset;
  widgets::toggle(c.vg, c.f, c.t, c.s, sl.rect(SheetControl::Reset), "Reset", "DEFAULTS", false,
                  m.pressed == SheetControl::Reset, &reset_icon);

  // powered by: the S mark and the wordmark, centred
  constexpr std::string_view kPowered = "POWERED BY SUEHR STUDIOS";
  const float text_w = draw::text_width(c.vg, c.f.ui_medium, 20.0F * c.s, kPowered, 3.2F * c.s);
  const float mark_h = 30.0F * c.s;
  const float mark_w = brand_width(mark_h);
  const float gap = 16.0F * c.s;
  const float x = sl.powered.cx() - (mark_w + gap + text_w) / 2;
  draw_brand_mark(c.vg, x, sl.powered.cy() - mark_h / 2, mark_h, rgba(0x9988CC, 0.9F));
  draw::text(c.vg, c.f.ui_medium, 20.0F * c.s, c.t.ink3, draw::kLeftMiddle, x + mark_w + gap, sl.powered.cy(), kPowered,
             3.2F * c.s);
}

}  // namespace

// ---------------------------------------------------------------------------
// layout
// ---------------------------------------------------------------------------

SheetLayout SheetLayout::compute(Page kind, const Rect& well, platform::Size panel) noexcept {
  SheetLayout sl{.kind = kind, .well = well, .panel = panel, .scale = well.w > 0 ? well.w / kRefW : 1.0F};
  if (kind == Page::None || kind == Page::Count) {
    return sl;
  }
  const float s = sl.scale;
  sl.frame = well;
  const float d = kHeadH * s;
  at(sl, SheetControl::Close) = {
      .x = sl.frame.right() - kPadSide * s - d, .y = sl.frame.y + kPadTop * s, .w = d, .h = d};
  if (kind != Page::Root) {
    at(sl, SheetControl::Back) = {.x = sl.frame.x + kPadSide * s, .y = sl.frame.y + kPadTop * s, .w = d, .h = d};
  }
  const float gx = sl.frame.x + kPadSide * s;
  const float gy = sl.frame.y + (kPadTop + kHeadH + kHeadGap) * s;
  const float grid_h = sl.frame.bottom() - kPadBottom * s - gy;
  switch (kind) {
    case Page::Root:
      place_root(sl, gx, gy);
      break;
    case Page::Audio:
      place_audio(sl, gx, gy, grid_h);
      break;
    case Page::Theme:
      place_theme(sl, gx, gy, grid_h);
      break;
    case Page::Units:
      place_units(sl, gx, gy, grid_h);
      break;
    case Page::Calibration:
      place_calibration(sl, gx, gy, grid_h);
      break;
    case Page::System:
      place_system(sl, gx, gy, grid_h);
      break;
    case Page::None:
    case Page::Count:
      break;
  }
  return sl;
}

PageLayouts compute_pages(const Layout& layout) noexcept {
  PageLayouts pages{};
  for (std::size_t i = 0; i < kPageCount; ++i) {
    pages[i] = SheetLayout::compute(static_cast<Page>(i), layout.well, layout.panel);
  }
  return pages;
}

std::optional<SheetControl> SheetLayout::hit_test(float x, float y) const noexcept {
  for (std::size_t i = 0; i < kSheetControlCount; ++i) {
    const auto k = static_cast<SheetControl>(i);
    Rect r = controls[i];
    if (r.w <= 0.0F) {
      continue;
    }
    if (is_slider(k)) {  // the label row above the track is part of the target
      r.y -= kSliderLabelH * scale;
      r.h += kSliderLabelH * scale;
    }
    if (r.contains(x, y)) {
      return k;
    }
  }
  return std::nullopt;
}

// ---------------------------------------------------------------------------
// drawing
// ---------------------------------------------------------------------------

void draw_sheet(Canvas* vg, const SheetLayout& sl, const Theme& t, const Fonts& f, const SheetModel& m) noexcept {
  if (sl.kind == Page::None || sl.kind == Page::Count) {
    return;
  }
  const Ctx c{.vg = vg, .t = t, .f = f, .s = sl.scale};
  draw::rounded_fill(vg, sl.frame, kRadius * sl.scale, t.sheet);
  draw::rounded_stroke(vg, sl.frame, kRadius * sl.scale, t.line2);
  head(c, sl, m);
  switch (sl.kind) {
    case Page::Root:
      draw_root(c, sl, m);
      break;
    case Page::Audio:
      draw_audio(c, sl, m);
      break;
    case Page::Theme:
      draw_theme(c, sl, m);
      break;
    case Page::Units:
      draw_units(c, sl, m);
      break;
    case Page::Calibration:
      draw_calibration(c, sl, m);
      break;
    case Page::System:
      draw_system(c, sl, m);
      break;
    case Page::None:
    case Page::Count:
      break;
  }
}

}  // namespace c3::render
