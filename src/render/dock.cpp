#include "dock.hpp"

#include "draw.hpp"
#include "icons.hpp"
#include "widgets.hpp"

#include <string_view>

namespace c3::render {
namespace {

// Reference geometry (mockup v6 #dock, 1520 × 392).
constexpr float kRefW = kReferenceContentW;
constexpr float kLightsW = 496.0F;  // under the fuel/systems column
constexpr float kIslandGap = 16.0F;
constexpr float kPadTop = 24.0F;
constexpr float kPadSide = 24.0F;
constexpr float kPadBottom = 20.0F;
constexpr float kLabelH = 28.0F;
constexpr float kLabelGap = 10.0F;
constexpr float kControlH = 96.0F;
constexpr float kRowGap = 10.0F;
constexpr float kHintH = 26.0F;
constexpr float kHintGap = 8.0F;
// letter spacing per text role, in mockup units (CSS em tracking × font size)
constexpr float kLabelTracking = 3.4F;  // .14em × 24
constexpr float kHintTracking = 1.4F;   // .06em × 24
constexpr float kLeverTracking = 3.0F;  // .14em × 22
constexpr float kToggleScale = 0.78F;   // dock toggles are the mockup's 26/19 px, not 34/22
constexpr float kSegScale = 0.9F;

constexpr std::array<std::string_view, 5> kBlowerCaptions = {"Off", "1", "2", "3", "4"};
constexpr std::array<std::string_view, 3> kModeCaptions = {"Vent", "Floor", "Defrost"};

// The segmented groups index their buttons by enumerator offset, and draw_dock() casts that
// offset to the vehicle enums, so both orders have to agree.
constexpr auto index_of = [](auto e) noexcept { return static_cast<std::size_t>(e); };
static_assert(index_of(DockControl::ModeFloor) == index_of(DockControl::ModeVent) + 1);
static_assert(index_of(DockControl::ModeDefrost) == index_of(DockControl::ModeVent) + 2);
static_assert(index_of(vehicle::HvacMode::Vent) == 0 && index_of(vehicle::HvacMode::Floor) == 1 &&
              index_of(vehicle::HvacMode::Defrost) == 2);
static_assert(index_of(DockControl::Blower4) == index_of(DockControl::BlowerOff) + 4);
static_assert(index_of(vehicle::HvacBlower::Off) == 0 && index_of(vehicle::HvacBlower::L4) == 4);

[[nodiscard]] constexpr Rect scaled(const Rect& dock, float s, float x, float y, float w, float h) noexcept {
  return {.x = dock.x + x * s, .y = dock.y + y * s, .w = w * s, .h = h * s};
}

void island(Canvas* vg, const Theme& t, float s, const Rect& r) noexcept {
  draw::rounded_fill(vg, r, widgets::kTileRadius * s, t.surface);
  draw::rounded_stroke(vg, r, widgets::kTileRadius * s, t.line);
}

// Island title, top-left, with an optional dimmer hint after it.
void label(Canvas* vg, const Fonts& f, const Theme& t, float s, const Rect& island_r, std::string_view title,
           std::string_view hint = {}) noexcept {
  const float x = island_r.x + (kPadSide + 8.0F) * s;
  const float y = island_r.y + (kPadTop + kLabelH / 2) * s;
  draw::text(vg, f.ui_medium, 24.0F * s, t.ink3, draw::kLeftMiddle, x, y, title, kLabelTracking * s);
  if (!hint.empty()) {
    const float tw = draw::text_width(vg, f.ui_medium, 24.0F * s, title, kLabelTracking * s);
    draw::text(vg, f.ui_medium, 19.0F * s, draw::alpha(t.ink3, 0.7F), draw::kLeftMiddle, x + tw + 16.0F * s, y, hint,
               kHintTracking * s);
  }
}

void segment_group(Canvas* vg, const Theme& t, float s, const Rect& group) noexcept {
  draw::rounded_fill(vg, group, widgets::kControlRadius * s, t.surface);
  draw::rounded_stroke(vg, group, widgets::kControlRadius * s, t.line);
}

}  // namespace

DockLayout DockLayout::compute(const Rect& dock) noexcept {
  DockLayout dl{.dock = dock, .scale = dock.w > 0 ? dock.w / kRefW : 1.0F};
  const float s = dl.scale;
  const float dock_h = dock.h / s;  // mockup units
  const float hvac_x = kLightsW + kIslandGap;
  const float hvac_w = kRefW - hvac_x;
  dl.lights_island = scaled(dock, s, 0, 0, kLightsW, dock_h);
  dl.hvac_island = scaled(dock, s, hvac_x, 0, hvac_w, dock_h);

  // HVAC: three 96-unit rows under the title.
  const float row1 = kPadTop + kLabelH + kLabelGap;
  const float row2 = row1 + kControlH + kRowGap;
  const float row3 = row2 + kControlH + kRowGap;
  const float inner_x = hvac_x + kPadSide;
  const float inner_w = hvac_w - 2 * kPadSide;
  dl.mode_group = scaled(dock, s, inner_x, row1, inner_w, kControlH);
  dl.blower_group = scaled(dock, s, inner_x, row2, inner_w, kControlH);
  widgets::split_segments(dl.mode_group, s,
                          std::span<Rect>(dl.controls).subspan(static_cast<std::size_t>(DockControl::ModeVent), 3));
  widgets::split_segments(dl.blower_group, s,
                          std::span<Rect>(dl.controls).subspan(static_cast<std::size_t>(DockControl::BlowerOff), 5));
  dl.controls[static_cast<std::size_t>(DockControl::Ac)] = scaled(dock, s, inner_x, row3, inner_w, kControlH);

  // Lights: three equal toggles between the title and the hint at the bottom.
  const float lights_w = kLightsW - 2 * kPadSide;
  const float col_top = row1;
  const float col_bottom = dock_h - kPadBottom - kHintH - kHintGap;
  const float toggle_h = (col_bottom - col_top - 2 * kRowGap) / 3;
  const std::array<DockControl, 3> order = {DockControl::Doors, DockControl::Headlamps, DockControl::HighBeams};
  for (std::size_t i = 0; i < order.size(); ++i) {
    dl.controls[static_cast<std::size_t>(order[i])] =
        scaled(dock, s, kPadSide, col_top + static_cast<float>(i) * (toggle_h + kRowGap), lights_w, toggle_h);
  }
  return dl;
}

std::optional<DockControl> DockLayout::hit_test(float x, float y) const noexcept {
  for (std::size_t i = 0; i < kDockControlCount; ++i) {
    if (controls[i].contains(x, y)) {
      return static_cast<DockControl>(i);
    }
  }
  return std::nullopt;
}

void draw_dock(Canvas* vg, const DockLayout& dl, const Theme& t, const Fonts& f, const DockModel& m) noexcept {
  const float s = dl.scale;
  const auto pressed = [&](DockControl c) noexcept { return m.pressed == c; };

  // ---- HVAC island
  island(vg, t, s, dl.hvac_island);
  label(vg, f, t, s, dl.hvac_island, "HVAC", m.live ? vehicle::to_string(m.command) : "STATE UNAVAILABLE");
  segment_group(vg, t, s, dl.mode_group);
  for (std::size_t i = 0; i < kModeCaptions.size(); ++i) {
    const auto c = static_cast<DockControl>(static_cast<std::size_t>(DockControl::ModeVent) + i);
    const auto mode = static_cast<vehicle::HvacMode>(i);
    widgets::segment_button(vg, f, t, s * kSegScale, dl.rect(c), kModeCaptions[i], m.live && m.hvac.mode == mode,
                            pressed(c));
  }
  segment_group(vg, t, s, dl.blower_group);
  for (std::size_t i = 0; i < kBlowerCaptions.size(); ++i) {
    const auto c = static_cast<DockControl>(static_cast<std::size_t>(DockControl::BlowerOff) + i);
    widgets::segment_button(vg, f, t, s * kSegScale, dl.rect(c), kBlowerCaptions[i],
                            m.live && static_cast<std::uint8_t>(m.hvac.blower) == i, pressed(c));
  }
  const Icon ac_icon = Icon::Snowflake;
  widgets::toggle(vg, f, t, s * kToggleScale, dl.rect(DockControl::Ac), "A/C",
                  !m.live ? "UNKNOWN" : (m.hvac.ac_enabled ? "ON" : "OFF"), m.live && m.hvac.ac_enabled,
                  pressed(DockControl::Ac), &ac_icon);

  // ---- Lights island
  island(vg, t, s, dl.lights_island);
  label(vg, f, t, s, dl.lights_island, "LIGHTS");
  const Icon doors_icon = Icon::Doors;
  const Icon lamp_icon = Icon::Headlamp;
  const Icon high_icon = Icon::HighBeam;
  const bool lamps_known = m.live && m.doors_known;
  widgets::toggle(vg, f, t, s * kToggleScale, dl.rect(DockControl::Doors), "Doors",
                  !lamps_known ? "UNKNOWN" : (m.lamps.lamp_doors_open ? "OPEN" : "CLOSED"),
                  lamps_known && m.lamps.lamp_doors_open, pressed(DockControl::Doors), &doors_icon);
  widgets::toggle(vg, f, t, s * kToggleScale, dl.rect(DockControl::Headlamps), "Lights",
                  !m.live ? "UNKNOWN" : (m.lamps.headlights_on ? "ON" : "OFF"), m.live && m.lamps.headlights_on,
                  pressed(DockControl::Headlamps), &lamp_icon);
  const bool high_known = m.live && m.high_known;
  widgets::toggle(vg, f, t, s * kToggleScale, dl.rect(DockControl::HighBeams), "High Beams",
                  !high_known ? "UNKNOWN" : (m.lamps.highbeams_on ? "ON" : "OFF"), high_known && m.lamps.highbeams_on,
                  pressed(DockControl::HighBeams), &high_icon);
  draw::text(vg, f.ui_medium, 19.0F * s, draw::alpha(t.ink3, 0.8F), draw::kCenterMiddle, dl.lights_island.cx(),
             dl.lights_island.bottom() - (kPadBottom + kHintH / 2) * s, "Parallel to the factory switches",
             kHintTracking * s);
}

void draw_lever_hint(Canvas* vg, const Rect& lever, const Theme& t, const Fonts& f) noexcept {
  const float s = lever.h > 0 ? lever.h / 36.0F : 1.0F;
  draw::text(vg, f.ui_medium, 22.0F * s, t.ink3, draw::kCenterMiddle, lever.cx(), lever.cy(),
             "\xE2\x86\x93 TEMPERATURE \xC2\xB7 MECHANICAL LEVER BELOW SCREEN", kLeverTracking * s);
}

}  // namespace c3::render
