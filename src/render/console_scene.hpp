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

enum class Severity : std::uint8_t {
  Alarm,    // oil, coolant
  Caution,  // fuel, charging
};

/// What a gauge shows for one channel, derived from faults[] and the live flag only.
struct GaugeStatus {
  GaugeState state = GaugeState::Stale;
  std::string_view text;  // Warn: caption ("OVERHEAT"); Open: sub-label ("SENDER OPEN")
};

/// Pure display-side mapping. `frame` may be null. Never compares a sensor value.
[[nodiscard]] GaugeStatus classify_gauge(const telemetry::Frame* frame, bool live, telemetry::Channel ch) noexcept;

struct WallClock {
  int hour12 = 12;
  int minute = 0;
  bool pm = false;
};

/// Everything the scene draws for the surfaces this tick.
struct WellView {
  TabsModel tabs;
  ToolsModel tools;
  Page page = Page::None;  // settings surface page, None when CarPlay or the camera shows
  SheetModel sheet_model;
  bool camera = false;
  CameraModel camera_model;
};

struct SceneInput {
  telemetry::TelemetryView telemetry;
  bool carplay_connected = false;
  DockModel dock;
  WallClock clock;
  float dt_s = 0.0F;  // since last update
  float t_s = 0.0F;   // since start, for animation
  // surfaces and settings
  Units units{};
  WellView well;
};

class ConsoleScene {
public:
  ConsoleScene(const Layout& layout, const Theme& theme, const Fonts& fonts) noexcept;

  /// Advances needle damping. Call once per frame before draw().
  void update(const SceneInput& in) noexcept;
  void draw(Canvas* vg, const SceneInput& in) const noexcept;

  void set_theme(const Theme& theme) noexcept { theme_ = theme; }
  [[nodiscard]] const Layout& layout() const noexcept { return layout_; }
  [[nodiscard]] const DockLayout& dock_layout() const noexcept { return dock_; }
  [[nodiscard]] const TabsLayout& tabs_layout() const noexcept { return tabs_; }
  [[nodiscard]] const ToolsLayout& tools_layout() const noexcept { return tools_; }

private:
  // Mechanical lag on the drawn value, distinct from the numeral (mockup: k = dt * rate).
  struct Damped {
    float value = 0.0F;
    bool primed = false;
    void step(float target, float rate, float dt_s) noexcept;
  };

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
  Damped oil_;
  Damped clt_;
  Damped fuel_percent_;
};

}  // namespace c3::render
