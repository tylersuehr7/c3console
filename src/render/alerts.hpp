// Alerts — banners stacked over the top of the CarPlay well and the status-bar chip.
//
// Built from faults[] and the link state only, ordered critical > caution > neutral. A
// low-oil banner overlays the map on purpose: it outranks navigation. The comms banner
// reports the age of the last frame and that HVAC holds its last state (invariant 6).
#pragma once

#include "canvas.hpp"
#include "fonts.hpp"
#include "layout.hpp"
#include "telemetry/frame.hpp"
#include "theme.hpp"
#include "units.hpp"
#include "utils/fixed_vector.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace c3::render {

// Ordered by priority: collect_alerts() sorts on the enumerator value.
enum class AlertLevel : std::uint8_t {
  Alarm,    // critical: red
  Caution,  // amber
  Neutral,  // informational: comms, non-critical sender faults
};

struct Alert {
  AlertLevel level = AlertLevel::Neutral;
  std::string_view title;         // static text ("Low oil pressure")
  std::string_view chip;          // short status-bar label ("OIL", "CHARGING")
  std::array<char, 48> detail{};  // formatted value in display units, NUL-terminated

  [[nodiscard]] std::string_view detail_view() const noexcept { return detail.data(); }
};

constexpr std::size_t kMaxAlerts = telemetry::kMaxFaults + 1;  // faults plus the comms banner
using AlertList = utils::FixedVector<Alert, kMaxAlerts>;

/// Builds the banner list for this frame: one per fault, sorted Alarm > Caution > Neutral and
/// stable within a level, or a single "No telemetry" banner when the link is not live.
///
/// @param frame the latest frame; may be null
/// @param live whether the link is live; when false the fault list is not consulted
/// @param age_ms time since the last good frame, shown in the comms banner; ignored when live
/// @param units display units for the values in the banners
[[nodiscard]] AlertList collect_alerts(const telemetry::Frame* frame, bool live, std::uint64_t age_ms,
                                       Units units = {}) noexcept;

/// Stacks the banners from the top of the well down, in list order.
void draw_alerts(Canvas* vg, const Rect& well, const Theme& t, const Fonts& f, const AlertList& alerts) noexcept;

/// Status-bar chip for the top alert, right-aligned at `right_x`. Draws nothing when clear.
void draw_alert_chip(Canvas* vg, float right_x, float mid_y, const Theme& t, const Fonts& f,
                     const AlertList& alerts) noexcept;

}  // namespace c3::render
