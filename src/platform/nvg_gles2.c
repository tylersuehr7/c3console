// NanoVG OpenGL 3 backend, instantiated once, adn compiled as C on-purpose (see nvg_gl.hpp).
// Third-party code: warnings are suppressed for this file in CMake.
#include <GLES2/gl2.h>

#define NANOVG_GLES2_IMPLEMENTATION
#include "nanovg.h"
#include "nanovg_gl.h"
