#include "theme.hpp"

#include <cmath>

namespace c3::render {

Color rgb(std::uint32_t hex) noexcept {
  return rgba(hex, 1.0F);
}

Color rgba(std::uint32_t hex, float alpha) noexcept {
  return nvgRGBAf(static_cast<float>((hex >> 16U) & 0xFFU) / 255.0F, static_cast<float>((hex >> 8U) & 0xFFU) / 255.0F,
                  static_cast<float>(hex & 0xFFU) / 255.0F, alpha);
}

float luminance(Color c) noexcept {
  const auto lin = [](float v) noexcept { return v <= 0.03928F ? v / 12.92F : std::pow((v + 0.055F) / 1.055F, 2.4F); };
  return 0.2126F * lin(c.r) + 0.7152F * lin(c.g) + 0.0722F * lin(c.b);
}

Theme Theme::dark() noexcept {
  return Theme{
      .bg = rgb(0x000000),
      .surface = rgba(0xFFFFFF, 0.035F),
      .surface2 = rgba(0xFFFFFF, 0.06F),
      .line = rgba(0xFFFFFF, 0.07F),
      .line2 = rgba(0xFFFFFF, 0.13F),
      .ink = rgb(0xF5F3EE),
      .ink2 = rgb(0x9A9EA6),
      .ink3 = rgb(0x5B6068),
      .ember = rgb(0xFF6A2B),
      .ember_ink = rgb(0x1A0C05),
      .amber = rgb(0xFFC24A),
      .amber_ink = rgb(0x1B1300),
      .alarm = rgb(0xFF3B30),
      .ok = rgb(0x34C759),
      .track = rgba(0xFFFFFF, 0.08F),
      .band = rgba(0xFFFFFF, 0.10F),
      .zone = rgba(0xFF3B30, 0.30F),
      .fill0 = rgb(0xCFCAC0),
      .fill1 = rgb(0xFFFFFF),
      .marker = rgb(0xFFFFFF),
      .glow = rgba(0xFFFFFF, 0.07F),
      .halo = rgba(0xFFFFFF, 0.40F),
      .banner_neutral = rgba(0x22252B, 0.96F),
      .banner_neutral_ink = rgb(0xF5F3EE),
      .well_bg = rgb(0x060810),
      .sheet = rgb(0x0C0D10),
  };
}

Theme Theme::light() noexcept {
  return Theme{
      .bg = rgb(0xF3F1EC),
      .surface = rgb(0xFFFFFF),
      .surface2 = rgba(0x000000, 0.05F),
      .line = rgba(0x000000, 0.08F),
      .line2 = rgba(0x000000, 0.14F),
      .ink = rgb(0x15171B),
      .ink2 = rgb(0x5A5F68),
      .ink3 = rgb(0x8B9099),
      .ember = rgb(0xFF6A2B),
      .ember_ink = rgb(0x1A0C05),
      .amber = rgb(0xE39A00),
      .amber_ink = rgb(0x1B1300),
      .alarm = rgb(0xE5322B),
      .ok = rgb(0x34C759),
      .track = rgba(0x000000, 0.08F),
      .band = rgba(0x000000, 0.09F),
      .zone = rgba(0xE5322B, 0.28F),
      .fill0 = rgb(0x4A4E56),
      .fill1 = rgb(0x15171B),
      .marker = rgb(0x15171B),
      .glow = rgba(0x000000, 0.06F),
      .halo = rgba(0x000000, 0.22F),
      .banner_neutral = rgba(0xFFFFFF, 0.96F),
      .banner_neutral_ink = rgb(0x15171B),
      .well_bg = rgb(0x060810),
      .sheet = rgb(0xFFFFFF),
  };
}

void Theme::set_accent(Color accent) noexcept {
  ember = accent;
  ember_ink = luminance(accent) > 0.25F ? rgb(0x141210) : rgb(0xFFFFFF);
}

}  // namespace c3::render
