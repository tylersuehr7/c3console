#include "fonts.hpp"

#include <array>
#include <string_view>

namespace c3::render {

namespace {

struct FontFile {
  int Fonts::* handle;
  std::string_view nvg_name;
  std::string_view file;
};

constexpr std::array<FontFile, 6> kFiles = {{
    {&Fonts::ui_regular, "ui-regular", "Inter-Regular.ttf"},
    {&Fonts::ui_medium, "ui-medium", "Inter-Medium.ttf"},
    {&Fonts::ui_semibold, "ui-semibold", "Inter-SemiBold.ttf"},
    {&Fonts::num_light, "num-light", "Barlow-Light.ttf"},
    {&Fonts::num_medium, "num-medium", "Barlow-Medium.ttf"},
    {&Fonts::num_semibold, "num-semibold", "Barlow-SemiBold.ttf"},
}};

}  // namespace

FontsResult load_fonts(Canvas* vg, const std::filesystem::path& dir) {
  if (vg == nullptr) {
    return {std::nullopt, "no NanoVG context"};
  }

  Fonts fonts;
  for (const FontFile& f : kFiles) {
    const std::filesystem::path path = dir / f.file;
    const std::string path_str = path.string();
    const std::string name(f.nvg_name);
    const int handle = nvgCreateFont(vg, name.c_str(), path_str.c_str());
    if (handle < 0) {
      return {std::nullopt, "cannot load font " + path_str};
    }
    fonts.*f.handle = handle;
  }

  return {fonts, {}};
}

}  // namespace c3::render
