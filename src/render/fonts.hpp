// Inter for UI labels, Barlow for numerals. Loaded once at init against the NanoVG context; handles are plain ints.
#pragma once

#include "canvas.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace c3::render {

struct Fonts {
  int ui_regular = -1;
  int ui_medium = -1;
  int ui_semibold = -1;
  int num_light = -1;
  int num_medium = -1;
  int num_semibold = -1;
};

struct FontsResult {
  std::optional<Fonts> fonts;  // set on success
  std::string error;
};

/// Loads assets/fonts/*.ttf from `dir`. Init-time: reads files, allocates in NanoVG.
[[nodiscard]] FontsResult load_fonts(Canvas* vg, const std::filesystem::path& dir);

}  // namespace c3::render
