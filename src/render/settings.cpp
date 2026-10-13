#include "settings.hpp"

#include "theme.hpp"

#include <algorithm>
#include <cmath>

namespace c3::render {

render::Theme make_theme(const Settings& s, bool headlights) noexcept {
  Theme t = effective_dark(s, headlights) ? Theme::dark() : Theme::light();
  t.set_accent(rgb(s.accent));
  return t;
}

std::uint32_t accent_from_hue(float hue_deg) noexcept {
  constexpr float kSat = 0.92F;
  constexpr float kLight = 0.58F;
  const float h = std::fmod(std::fmod(hue_deg, 360.0F) + 360.0F, 360.0F);
  const auto channel = [&](float n) noexcept {
    const float k = std::fmod(n + h / 30.0F, 12.0F);
    const float a = kSat * std::min(kLight, 1.0F - kLight);
    const float v = kLight - a * std::max(-1.0F, std::min({k - 3.0F, 9.0F - k, 1.0F}));
    return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F));
  };
  return (channel(0.0F) << 16U) | (channel(8.0F) << 8U) | channel(4.0F);
}

float hue_of(std::uint32_t rgb) noexcept {
  const float r = static_cast<float>((rgb >> 16U) & 0xFFU) / 255.0F;
  const float g = static_cast<float>((rgb >> 8U) & 0xFFU) / 255.0F;
  const float b = static_cast<float>(rgb & 0xFFU) / 255.0F;
  const float mx = std::max({r, g, b});
  const float mn = std::min({r, g, b});
  const float d = mx - mn;

  if (d <= 0.0F) {
    return 0.0F;
  }

  float hue = 0.0F;
  if (mx == r) {
    hue = std::fmod((g - b) / d, 6.0F);
  } else if (mx == g) {
    hue = (b - r) / d + 2.0F;
  } else {
    hue = (r - g) / d + 4.0F;
  }

  return std::fmod(hue * 60.0F + 360.0F, 360.0F);
}

std::optional<std::size_t> swatch_index(std::uint32_t rgb) noexcept {
  for (std::size_t i = 0; i < kSwatches.size(); ++i) {
    if (kSwatches[i].rgb == rgb) {
      return i;
    }
  }
  return std::nullopt;
}

}  // namespace c3::render
