// NanoVG OpenGL 3 backend, instantiated once, adn compiled as C on-purpose (see nvg_gl.hpp).
// Third-party code: warnings are suppressed for this file in CMake.
#include "gl_headers.h"

#define NANOVG_GL3_IMPLEMENTATION
#include "nanovg.h"
#include "nanovg_gl.h"
