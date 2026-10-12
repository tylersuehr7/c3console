// The one place OpenGL headers are included, for both C and C++ translation units.
//
// macOS ships GL 3.2 core prototypes in <OpenGL/gl3.h>. Linux desktop Mesa exports the
// prototypes from libGL when GL_GLEXT_PROTOTYPES is defined, so no loader is needed.
// The target (DrmBackend) includes <GLES2/gl2.h> directly and never includes this file.
#pragma once

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES 1  // NOLINT(cppcoreguidelines-macro-usage) required by <GL/glext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#endif
