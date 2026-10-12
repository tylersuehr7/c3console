// C++ view of the NanoVG GL backends: GL3 (nvg_gl3.c, desktop) and GLES2 (nvg_gles2.c, target).
//
// nanovg_gl.hpp is header-only and must be instantiated in exactly one translation unit per
// backend, with GL headers included first. Those units are C so the header compiles as its
// author intended; this header exposes just the entry points and the creation flags. Each
// build links exactly one of the two units.
#pragma once

struct NVGcontext;

namespace c3::platform::nvg {

// Mirrors NVGcreateFlags in nanovg_gl.h.
constexpr int kAntialias = 1 << 0;
constexpr int kStencilStrokes = 1 << 1;
constexpr int kDebug = 1 << 2;

}  // namespace c3::platform::nvg

extern "C" {
// NanoVG's own names; not ours to rename.
NVGcontext* nvgCreateGL3(int flags);    // NOLINT(readability-identifier-naming)
void nvgDeleteGL3(NVGcontext* ctx);     // NOLINT(readability-identifier-naming)
NVGcontext* nvgCreateGLES2(int flags);  // NOLINT(readability-identifier-naming)
void nvgDeleteGLES2(NVGcontext* ctx);   // NOLINT(readability-identifier-naming)
}
