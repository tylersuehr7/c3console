// ConsoleScene — the whole portrait panel for one frame.
//
// The scene owns the sub-layouts (dock, tabs, tools, sheets, camera) computed once from the
// Layout, plus the needle damping that carries state between frames. Everything else it draws
// comes from the SceneInput for that tick, so a frame is a pure function of its input and the
// damped needles. Gauge state is derived from faults[] and the live flag only: the MCU decides
// what is a fault (safety invariant 1), and a stale link greys the gauges out (invariant 4).
#pragma once

#include "alerts.hpp"
#include "camera_surface.hpp"
#include "canvas.hpp"
#include "dock.hpp"
#include "fonts.hpp"
#include "layout.hpp"
#include "sheet.hpp"
#include "status_tools.hpp"
#include "tabs.hpp"
#include "telemetry/sensor_source.hpp"
#include "telemetry/snapshot.hpp"
#include "theme.hpp"
#include "units.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace c3::render {

enum class GaugeState : std::uint8_t {
  Ok,
  Warn,         // a fault for this channel is in faults[]
  SenderFault,  // SENDER_OPEN / SENDER_SHORT for this channel
  Stale,        // no live frame
};

/// How a channel's warning is presented; the thresholds themselves live in the MCU.
enum class Severity : std::uint8_t {
  Alarm,    // oil, coolant: red
  Caution,  // fuel, charging: amber
};

/// What a gauge shows for one channel, derived from faults[] and the live flag only.
struct GaugeStatus {
  GaugeState state = GaugeState::Stale;
  std::string_view text;  // Warn: caption ("OVERHEAT"); SenderFault: sub-label ("SENDER OPEN")
};

/// Maps the frame's fault list to a gauge state for `ch`. Pure and display-side: it never
/// compares a sensor value against a threshold.
///
/// A sender fault for the channel outranks a value fault; with no live frame every channel is
/// Stale. `frame` may be null.
///
/// @return the state and the caption to draw with it
[[nodiscard]] GaugeStatus classify_gauge(const telemetry::Frame* frame, bool live, telemetry::Channel ch) noexcept;

/// Mechanical lag on a drawn needle. The numeral shows the exact value; the needle follows it
/// as in the mockup, moving `rate × dt` of the remaining distance per step.
struct DampedValue {
  float value = 0.0F;
  bool primed = false;  // the first target is adopted outright so the needle never sweeps in from zero

  /// Moves `value` toward `target`. A step of `rate_per_s × dt_s >= 1` lands on the target.
  void step(float target, float rate_per_s, float dt_s) noexcept;
};

/// Status-bar clock, already in 12-hour form.
struct WallClock {
  int hour12 = 12;
  int minute = 0;
  bool pm = false;
};

/// What occupies the well this tick and the chrome around it.
struct WellView {
  TabsModel tabs;
  ToolsModel tools;
  Page page = Page::None;  // settings page in the well; None when CarPlay or the camera shows
  SheetModel sheet_model;
  bool camera = false;  // the camera surface takes priority over `page`
  CameraModel camera_model;
};

/// Everything the scene needs for one frame.
struct SceneInput {
  telemetry::TelemetryView telemetry;
  bool carplay_connected = false;  // false draws the "connecting" state in the well
  DockModel dock;
  WallClock clock;
  float dt_s = 0.0F;  // since the last update(); drives needle damping
  float t_s = 0.0F;   // since start; drives the spinner
  Units units{};      // display units for the oil and coolant gauges and the banners
  WellView well;
};

class ConsoleScene {
public:
  ConsoleScene(const Layout& layout, const Theme& theme, const Fonts& fonts) noexcept;

  /// Advances needle damping toward the frame's values. Call once per frame before draw(); a
  /// null frame leaves the needles where they are.
  void update(const SceneInput& in) noexcept;

  /// Draws the complete panel. No allocation; every nvgSave is paired with a restore.
  void draw(Canvas* vg, const SceneInput& in) const noexcept;

  void set_theme(const Theme& theme) noexcept { theme_ = theme; }
  [[nodiscard]] const Layout& layout() const noexcept { return layout_; }
  [[nodiscard]] const DockLayout& dock_layout() const noexcept { return dock_; }
  [[nodiscard]] const TabsLayout& tabs_layout() const noexcept { return tabs_; }
  [[nodiscard]] const ToolsLayout& tools_layout() const noexcept { return tools_; }
  [[nodiscard]] const SheetLayout& sheet_layout(Page page) const noexcept {
    return sheets_[static_cast<std::size_t>(page)];
  }
  [[nodiscard]] const CameraLayout& camera_layout() const noexcept { return camera_; }

private:
  void draw_status_bar(Canvas* vg, const SceneInput& in, const AlertList& alerts) const noexcept;
  void draw_systems(Canvas* vg, const SceneInput& in) const noexcept;
  void draw_well(Canvas* vg, const SceneInput& in) const noexcept;

  Layout layout_;
  DockLayout dock_;
  TabsLayout tabs_;
  ToolsLayout tools_;
  PageLayouts sheets_;  // indexed by Page
  CameraLayout camera_;
  Theme theme_;
  Fonts fonts_;
  DampedValue oil_;           // psi; converted to display units at draw time
  DampedValue clt_;           // °F
  DampedValue fuel_percent_;  // 0..100
};

}  // namespace c3::render
