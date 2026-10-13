// Dock — the two control islands at the bottom of the panel: Lights (doors, lights, high beams)
// and HVAC (mode, blower, A/C). The dock only ever shows the relay state the MCU re-asserts in
// each frame, never the console's own optimism; with no live frame every control reads UNKNOWN.
#pragma once

#include "canvas.hpp"
#include "fonts.hpp"
#include "layout.hpp"
#include "theme.hpp"
#include "vehicle/vehicle_commands.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace c3::render {

enum class DockControl : std::uint8_t {
  BlowerOff,
  Blower1,
  Blower2,
  Blower3,
  Blower4,
  Ac,
  ModeVent,
  ModeFloor,
  ModeDefrost,
  Headlamps,  // "Lights" in the UI; the parallel relay across the factory switch
  Doors,
  HighBeams,
  Count,
};
constexpr std::size_t kDockControlCount = static_cast<std::size_t>(DockControl::Count);

struct DockLayout {
  Rect dock;
  float scale = 1.0F;  // dock pixels per mockup unit (1520-wide reference)
  Rect lights_island;
  Rect hvac_island;
  Rect blower_group;                             // segmented container holding BlowerOff..Blower4
  Rect mode_group;                               // segmented container holding ModeVent..ModeDefrost
  std::array<Rect, kDockControlCount> controls;  // indexed by DockControl

  /// Lays the islands and controls out inside `dock`, scaled from the mockup's 1520 × 392.
  [[nodiscard]] static DockLayout compute(const Rect& dock) noexcept;

  /// The control under (x, y), if any. Island chrome and labels are not controls.
  [[nodiscard]] std::optional<DockControl> hit_test(float x, float y) const noexcept;

  [[nodiscard]] const Rect& rect(DockControl c) const noexcept { return controls[static_cast<std::size_t>(c)]; }
};

/// What the dock shows: the MCU's re-asserted relay state, and the control under a finger.
struct DockModel {
  vehicle::HvacState hvac;
  vehicle::LampState lamps;
  std::optional<DockControl> pressed;
  bool live = false;         // false: every control reads UNKNOWN and nothing is selected
  bool doors_known = false;  // the frame carried `relays.doors`
  bool high_known = false;   // the frame carried `relays.high`
  vehicle::CommandStatus command = vehicle::CommandStatus::Idle;  // shown as the HVAC island's hint
};

void draw_dock(Canvas* vg, const DockLayout& dl, const Theme& t, const Fonts& f, const DockModel& m) noexcept;

/// The one-line hint above the mechanical temperature lever below the screen.
void draw_lever_hint(Canvas* vg, const Rect& lever, const Theme& t, const Fonts& f) noexcept;

}  // namespace c3::render
