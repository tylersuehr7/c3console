// Canvas — the render library's names for NanoVG's types. Everything that draws takes a
// `Canvas*` obtained from the display backend between begin_frame() and end_frame().
#pragma once

#include "nanovg.h"

namespace c3::render {

using Canvas = NVGcontext;
using Color = NVGcolor;
using Paint = NVGpaint;

}  // namespace c3::render
