#include "desktop_display_backend.hpp"

#include "gl_headers.hpp"
#include "logging/log.hpp"
#include "nanovg.h"
#include "nvg_gl.hpp"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace c3::platform {

namespace {

constexpr std::string_view kUnit = "display";

struct FileCloser {
  // unique_ptr is the owner here; the project does not use gsl::owner<>.
  void operator()(std::FILE* f) const noexcept {
    static_cast<void>(std::fclose(f));  // NOLINT(cppcoreguidelines-owning-memory)
  }
};
using FilePtr = std::unique_ptr<std::FILE, FileCloser>;

// Window size in points that fits `panel` into `fraction` of the usable display area.
Size fit_window_to_display(Size panel, float fraction) noexcept {
  SDL_Rect usable{};

  if (SDL_GetDisplayUsableBounds(0, &usable) != 0 || usable.w <= 0 || usable.h <= 0) {
    usable = SDL_Rect{0, 0, 1280, 800};
  }

  const float max_w = static_cast<float>(usable.w) * fraction;
  const float max_h = static_cast<float>(usable.h) * fraction;
  const float scale = std::min(max_w / static_cast<float>(panel.width), max_h / static_cast<float>(panel.height));

  return {
      .width = static_cast<int>(static_cast<float>(panel.width) * scale),
      .height = static_cast<int>(static_cast<float>(panel.height) * scale),
  };
}

}  // namespace

void DesktopDisplayBackend::WindowDeleter::operator()(SDL_Window* window) const noexcept {
  SDL_DestroyWindow(window);
}

void DesktopDisplayBackend::GlContextDeleter::operator()(void* ctx) const noexcept {
  SDL_GL_DeleteContext(ctx);
}

void DesktopDisplayBackend::NvgDeleter::operator()(NVGcontext* ctx) const noexcept {
  nvgDeleteGL3(ctx);
}

DesktopDisplayBackend::DesktopDisplayBackend(Size panel, float dpi) noexcept : panel_(panel), dpi_(dpi) {}

DesktopDisplayBackend::~DesktopDisplayBackend() {
  // Members release in reverse order: NanoVG needs the GL context current, so drop it
  // first while the context still exists, then the context, then the window.
  nvg_.reset();
  gl_.reset();
  window_.reset();
  if (sdl_video_owned_) {
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
  }
}

DesktopDisplayBackend::Result DesktopDisplayBackend::create(const Options& options) {
  if (options.panel.width <= 0 || options.panel.height <= 0) {
    return {nullptr, "panel size must be positive"};
  }

  std::unique_ptr<DesktopDisplayBackend> b(new DesktopDisplayBackend(options.panel, options.dpi));

  if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
    return {nullptr, std::string("SDL_Init: ") + SDL_GetError()};
  }
  b->sdl_video_owned_ = true;

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);  // NanoVG stencil strokes

  const Size win = fit_window_to_display(options.panel, options.fit_fraction);
  const std::string title(options.title);
  b->window_.reset(SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win.width,
                                    win.height, SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE));
  if (!b->window_) {
    return {nullptr, std::string("SDL_CreateWindow: ") + SDL_GetError()};
  }

  b->gl_.reset(SDL_GL_CreateContext(b->window_.get()));
  if (!b->gl_) {
    return {nullptr, std::string("SDL_GL_CreateContext: ") + SDL_GetError()};
  }
  if (SDL_GL_MakeCurrent(b->window_.get(), b->gl_.get()) != 0) {
    return {nullptr, std::string("SDL_GL_MakeCurrent: ") + SDL_GetError()};
  }
  SDL_GL_SetSwapInterval(1);  // vsync paces the loop

  b->nvg_.reset(nvgCreateGL3(nvg::kAntialias | nvg::kStencilStrokes));
  if (!b->nvg_) {
    return {nullptr, "nvgCreateGL3 failed (GL 3.2 core context required)"};
  }

  b->refresh_viewport();

  // GL hands back const GLubyte*; reinterpreting as char is the documented way to read it.
  const auto* version =  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      reinterpret_cast<const char*>(glGetString(GL_VERSION));

  logging::debug(kUnit, "desktop backend: window ", win.width, "x", win.height, " pt, drawable ",
                 b->viewport_.drawable_w, "x", b->viewport_.drawable_h, " px, panel ", options.panel.width, "x",
                 options.panel.height, " @ scale ", b->viewport_.scale, ", GL ", version != nullptr ? version : "?");

  return {std::move(b), {}};
}

void DesktopDisplayBackend::refresh_viewport() noexcept {
  int dw = 0, dh = 0, ww = 0, wh = 0;
  SDL_GL_GetDrawableSize(window_.get(), &dw, &dh);
  SDL_GetWindowSize(window_.get(), &ww, &wh);
  viewport_ = fit_panel(panel_, dw, dh, ww, wh);
}

NVGcontext* DesktopDisplayBackend::begin_frame() noexcept {
  if (in_frame_ || !nvg_) {
    return nullptr;
  }

  refresh_viewport();

  const PanelViewport& vp = viewport_;
  glViewport(0, 0, vp.drawable_w, vp.drawable_h);
  glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  // Draw in drawable pixels with ratio 1 so NanoVG's anti-aliasing fringe is one real pixel,
  // then map panel space into the letterboxed area.
  nvgBeginFrame(nvg_.get(), static_cast<float>(vp.drawable_w), static_cast<float>(vp.drawable_h), 1.0F);
  nvgTranslate(nvg_.get(), vp.offset_x, vp.offset_y);
  nvgScale(nvg_.get(), vp.scale, vp.scale);
  in_frame_ = true;

  return nvg_.get();
}

PresentResult DesktopDisplayBackend::end_frame() noexcept {
  if (!in_frame_) {
    return PresentResult::Failed;
  }

  nvgEndFrame(nvg_.get());
  if (capture_pending_) {
    write_capture();
  }

  SDL_GL_SwapWindow(window_.get());
  in_frame_ = false;

  return PresentResult::Presented;
}

bool DesktopDisplayBackend::request_capture(std::string_view ppm_path) noexcept {
  if (capture_pending_ || ppm_path.empty() || ppm_path.size() > kMaxCapturePath) {
    return false;
  }
  std::copy(ppm_path.begin(), ppm_path.end(), capture_path_.begin());
  capture_path_[ppm_path.size()] = '\0';
  capture_pending_ = true;
  return true;
}

// Reads the back buffer (the frame just drawn, before swap) and writes a binary PPM.
// Development path: allocates, touches the filesystem, and is never hit in steady state.
void DesktopDisplayBackend::write_capture() noexcept {
  capture_pending_ = false;
  const int w = viewport_.drawable_w;
  const int h = viewport_.drawable_h;
  if (w <= 0 || h <= 0) {
    return;
  }

  const std::size_t row = static_cast<std::size_t>(w) * 3;
  std::vector<unsigned char> pixels;
  try {
    pixels.resize(row * static_cast<std::size_t>(h));
  } catch (...) {
    logging::error(kUnit, "capture: out of memory");
    return;
  }

  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadBuffer(GL_BACK);
  glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

  const FilePtr f(std::fopen(capture_path_.data(), "wb"));
  if (!f) {
    logging::error(kUnit, "capture: cannot open ", capture_path_.data());
    return;
  }

  static_cast<void>(std::fprintf(f.get(), "P6\n%d %d\n255\n", w, h));
  for (int y = h - 1; y >= 0; --y) {  // GL rows are bottom-up; PPM is top-down
    static_cast<void>(std::fwrite(&pixels[static_cast<std::size_t>(y) * row], 1, row, f.get()));
  }

  logging::info(kUnit, "capture written: ", capture_path_.data(), " (", w, "x", h, ")");
}

}  // namespace c3::platform
