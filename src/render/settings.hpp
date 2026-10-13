#pragma once

#include "units.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace c3::render {
struct Theme;

enum class ThemePreference : std::uint8_t {
  Dark,
  Light,
  Auto,  // follows the headlamp relay: light by day, dark once the lamps are on
};
constexpr std::array<ThemePreference, 3> kAllThemePreferences = {ThemePreference::Dark, ThemePreference::Light,
                                                                 ThemePreference::Auto};

[[nodiscard]] constexpr std::string_view to_string(ThemePreference p) noexcept {
  switch (p) {
    case ThemePreference::Dark:
      return "dark";
    case ThemePreference::Light:
      return "light";
    case ThemePreference::Auto:
      return "auto";
  }
  return "dark";
}

struct Swatch {
  std::string_view name;
  std::uint32_t rgb;
};
/// The nine accent swatches, chosen clear of the alarm red and caution amber hues.
constexpr std::array<Swatch, 9> kSwatches = {{{"Ember", 0xFF6A2B},
                                              {"Copper", 0xE0894A},
                                              {"Ice", 0x38BDF8},
                                              {"Teal", 0x2DD4BF},
                                              {"Mint", 0x34D399},
                                              {"Indigo", 0x6366F1},
                                              {"Violet", 0xA78BFA},
                                              {"Rose", 0xF472B6},
                                              {"Silver", 0xD4D4D8}}};

struct Settings {
  ThemePreference theme = ThemePreference::Dark;
  std::uint32_t accent = kSwatches[0].rgb;  // 0xRRGGBB
  Units units{};

  [[nodiscard]] constexpr bool operator==(const Settings&) const noexcept = default;
};

/// The dark/light choice in effect: Auto resolves from the headlamp relay.
[[nodiscard]] constexpr bool effective_dark(const Settings& s, bool headlights) noexcept {
  return s.theme == ThemePreference::Auto ? headlights : s.theme == ThemePreference::Dark;
}

/// Builds the render theme for the settings: dark or light, accent applied.
[[nodiscard]] Theme make_theme(const Settings& s, bool headlights) noexcept;

/// Custom hue at the mockup's fixed 92 % saturation, 58 % lightness. `hue_deg` 0..360.
[[nodiscard]] std::uint32_t accent_from_hue(float hue_deg) noexcept;

/// Hue in degrees of an 0xRRGGBB colour; 0 for greys.
[[nodiscard]] float hue_of(std::uint32_t rgb) noexcept;

/// Index into kSwatches when `rgb` is one of them.
[[nodiscard]] std::optional<std::size_t> swatch_index(std::uint32_t rgb) noexcept;

}  // namespace c3::render
