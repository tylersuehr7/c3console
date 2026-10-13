#include "alerts.hpp"

#include "draw.hpp"
#include "telemetry/fault_codes.hpp"
#include "utils/fixed_format.hpp"

#include <algorithm>

namespace c3::render {

namespace {

using telemetry::Channel;
using telemetry::Fault;
using telemetry::FaultKind;
using telemetry::Frame;

constexpr float kBannerH = 104.0F;
constexpr float kBannerGap = 12.0F;
constexpr float kBannerInset = 24.0F;
constexpr float kBannerRadius = 26.0F;

[[nodiscard]] Alert value_alert(const Frame& f, const Fault& x, Units units) noexcept {
  Alert a;
  switch (x.kind) {
    case FaultKind::OilLow: {
      a = {.level = AlertLevel::Alarm, .title = "Low oil pressure", .chip = "OIL"};
      const UnitScale scale = pressure_scale(units.pressure);
      static_cast<void>(utils::format_into(a.detail, "%.*f %.*s", scale.decimals,
                                           static_cast<double>(to_display_pressure(units.pressure, f.oil_psi)),
                                           static_cast<int>(scale.unit.size()), scale.unit.data()));
      break;
    }
    case FaultKind::CltHigh: {
      a = {.level = AlertLevel::Alarm, .title = "Coolant overheat", .chip = "COOLANT"};
      const UnitScale scale = temp_scale(units.temp);
      static_cast<void>(utils::format_into(a.detail, "%.*f %.*s", scale.decimals,
                                           static_cast<double>(to_display_temp(units.temp, f.coolant_f)),
                                           static_cast<int>(scale.unit.size()), scale.unit.data()));
      break;
    }
    case FaultKind::FuelLow:
      a = {.level = AlertLevel::Caution, .title = "Low fuel", .chip = "FUEL"};
      static_cast<void>(
          utils::format_into(a.detail, "%.0f%% remaining", static_cast<double>(f.fuel_fraction * 100.0F)));
      break;
    case FaultKind::VoltLow:
      a = {.level = AlertLevel::Caution, .title = "Charging system", .chip = "CHARGING"};
      static_cast<void>(utils::format_into(a.detail, "%.1f V \xC2\xB7 low", static_cast<double>(f.voltage_v)));
      break;
    case FaultKind::VoltHigh:
      a = {.level = AlertLevel::Caution, .title = "Charging system", .chip = "CHARGING"};
      static_cast<void>(utils::format_into(a.detail, "%.1f V \xC2\xB7 high", static_cast<double>(f.voltage_v)));
      break;
    case FaultKind::SenderOpen:
    case FaultKind::SenderShort: {
      // Oil and coolant senders are engine-critical; fuel and voltage are not.
      const bool critical = x.channel == Channel::Oil || x.channel == Channel::Coolant;
      a.level = critical ? AlertLevel::Alarm : AlertLevel::Neutral;
      a.chip = "SENDER";
      const bool open = x.kind == FaultKind::SenderOpen;
      switch (x.channel) {
        case Channel::Oil:
          a.title = open ? "Oil pressure sender open" : "Oil pressure sender short";
          break;
        case Channel::Coolant:
          a.title = open ? "Coolant sender open" : "Coolant sender short";
          break;
        case Channel::Fuel:
          a.title = open ? "Fuel sender open" : "Fuel sender short";
          break;
        case Channel::Volt:
        case Channel::Count:
          a.title = open ? "Voltage sense open" : "Voltage sense short";
          break;
      }
      static_cast<void>(utils::format_into(a.detail, "%s", "check wiring"));
      break;
    }
    case FaultKind::Count:
      break;
  }
  return a;
}

[[nodiscard]] Color fill_for(AlertLevel level, const Theme& t) noexcept {
  switch (level) {
    case AlertLevel::Alarm:
      return draw::alpha(t.alarm, 0.94F);
    case AlertLevel::Caution:
      return draw::alpha(t.amber, 0.96F);
    case AlertLevel::Neutral:
      return t.banner_neutral;
  }
  return t.surface2;
}

[[nodiscard]] Color ink_for(AlertLevel level, const Theme& t) noexcept {
  switch (level) {
    case AlertLevel::Alarm:
      return rgb(0xFFFFFF);
    case AlertLevel::Caution:
      return t.amber_ink;
    case AlertLevel::Neutral:
      return t.banner_neutral_ink;
  }
  return t.ink;
}

}  // namespace

AlertList collect_alerts(const Frame* frame, bool live, std::uint64_t age_ms, Units units) noexcept {
  AlertList out;

  if (!live || frame == nullptr) {
    Alert a{.level = AlertLevel::Neutral, .title = "No telemetry", .chip = "LINK LOST"};
    if (frame == nullptr) {
      static_cast<void>(utils::format_into(a.detail, "%s", "waiting for MCU"));
    } else {
      static_cast<void>(utils::format_into(a.detail, "%.1f s since last frame \xC2\xB7 HVAC holds last state",
                                           static_cast<double>(age_ms) / 1000.0));
    }
    static_cast<void>(out.push_back(a));
    return out;
  }

  for (const Fault& x : frame->faults) {
    static_cast<void>(out.push_back(value_alert(*frame, x, units)));
  }

  // Stable insertion sort needs no temporary heap buffer; the list holds at most 8 faults.
  for (std::size_t i = 1; i < out.size(); ++i) {
    const Alert current = out[i];
    std::size_t position = i;
    while (position > 0 && current.level < out[position - 1].level) {
      out[position] = out[position - 1];
      --position;
    }
    out[position] = current;
  }

  return out;
}

void draw_alerts(Canvas* vg, const Rect& well, const Theme& t, const Fonts& f, const AlertList& alerts) noexcept {
  const float s = well.w > 0 ? well.w / kReferenceContentW : 1.0F;
  float y = well.y + kBannerInset * s;
  const float x = well.x + kBannerInset * s;
  const float w = well.w - 2 * kBannerInset * s;
  const float h = kBannerH * s;

  for (const Alert& a : alerts) {
    const Rect r{.x = x, .y = y, .w = w, .h = h};
    draw::rounded_fill(vg, r, kBannerRadius * s, fill_for(a.level, t));

    if (a.level == AlertLevel::Neutral) {
      draw::rounded_stroke(vg, r, kBannerRadius * s, t.line2);
    }

    const Color ink = ink_for(a.level, t);
    draw::text(vg, f.ui_semibold, 32.0F * s, ink, draw::kLeftMiddle, r.x + 30.0F * s, r.cy(), a.title, 1.3F * s);
    draw::text(vg, f.num_medium, 30.0F * s, draw::alpha(ink, 0.9F), draw::kRightMiddle, r.right() - 30.0F * s, r.cy(),
               a.detail_view());

    y += h + kBannerGap * s;
  }
}

void draw_alert_chip(Canvas* vg, float right_x, float mid_y, const Theme& t, const Fonts& f,
                     const AlertList& alerts) noexcept {
  if (alerts.empty()) {
    return;
  }

  const Alert& top = alerts[0];
  std::array<char, 16> buf{};
  const std::string_view caption =
      alerts.size() == 1 ? top.chip : utils::format_into(buf, "%zu FAULTS", static_cast<std::size_t>(alerts.size()));
  constexpr float kChipH = 48.0F;
  constexpr float kChipPad = 22.0F;
  const float tw = draw::text_width(vg, f.ui_semibold, 24.0F, caption, 2.4F);
  const Rect r{.x = right_x - tw - 2 * kChipPad, .y = mid_y - kChipH / 2, .w = tw + 2 * kChipPad, .h = kChipH};

  draw::rounded_fill(vg, r, kChipH / 2, top.level == AlertLevel::Neutral ? t.surface2 : fill_for(top.level, t));

  if (top.level == AlertLevel::Neutral) {
    draw::rounded_stroke(vg, r, kChipH / 2, t.line2);
  }

  draw::text(vg, f.ui_semibold, 24.0F, top.level == AlertLevel::Neutral ? t.ink2 : ink_for(top.level, t),
             draw::kCenterMiddle, r.cx(), r.cy(), caption, 2.4F);
}

}  // namespace c3::render
