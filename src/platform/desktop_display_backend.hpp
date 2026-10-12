// DesktopBackend — SDL2 window + OpenGL 3.2 core context + NanoVG GL3.
//
// Development backend only; never linked into the drm build. The window is sized to fit
// the current display and the logical panel is scaled into it (platform/panel_viewport.hpp),
// so scene code draws in panel pixels exactly as it will on the target.
#pragma once

#include "debug_frame_capture.hpp"
#include "display_backend.hpp"
#include "panel_viewport.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

// Sourced from <nanovg.h>
struct SDL_Window;
struct NVGcontext;

namespace c3::platform {

class DesktopDisplayBackend final : public DisplayBackend, public DebugFrameCapture {
public:
  struct Options {
    Size panel{.width = 1600, .height = 2560};  // 800x1280 panel drawn at 2x
    float dpi = 264.0F;                         // reported for the target panel, not laptop
    float fit_fraction = 0.9F;                  // share of the usable display the window may take
    std::string_view title = "C3Console";
  };

  struct Result {
    std::unique_ptr<DesktopDisplayBackend> backend;  // null when `error` is set
    std::string error;
  };

  /// Initialises SDL video, opens the window, creates the GL context and the NanoVG
  /// context. Init-time; may allocate. Failure is a message, never an exception.
  [[nodiscard]] static Result create(const Options& options);

  ~DesktopDisplayBackend() override;
  DesktopDisplayBackend(const DesktopDisplayBackend&) = delete;
  DesktopDisplayBackend& operator=(const DesktopDisplayBackend&) = delete;
  DesktopDisplayBackend(DesktopDisplayBackend&&) = delete;
  DesktopDisplayBackend& operator=(DesktopDisplayBackend&&) = delete;

  [[nodiscard]] Size size() const noexcept override { return panel_; }
  [[nodiscard]] float dpi() const noexcept override { return dpi_; }
  [[nodiscard]] NVGcontext* context() const noexcept override { return nvg_.get(); }
  [[nodiscard]] NVGcontext* begin_frame() noexcept override;
  [[nodiscard]] PresentResult end_frame() noexcept override;
  [[nodiscard]] bool request_capture(std::string_view ppm_path) noexcept override;
  [[nodiscard]] PanelViewport viewport() const noexcept { return viewport_; }
  [[nodiscard]] SDL_Window* window() const noexcept { return window_.get(); }

private:  // RAII for NanoVG pointers
  struct WindowDeleter {
    void operator()(SDL_Window*) const noexcept;
  };
  struct GlContextDeleter {
    void operator()(void*) const noexcept;
  };
  struct NvgDeleter {
    void operator()(NVGcontext*) const noexcept;
  };

private:
  DesktopDisplayBackend(Size panel, float dpi) noexcept;
  void refresh_viewport() noexcept;
  void write_capture() noexcept;

private:
  Size panel_;
  float dpi_;
  bool sdl_video_owned_ = false;
  std::unique_ptr<SDL_Window, WindowDeleter> window_;
  std::unique_ptr<void, GlContextDeleter> gl_;
  std::unique_ptr<NVGcontext, NvgDeleter> nvg_;
  PanelViewport viewport_{};
  bool in_frame_ = false;

  static constexpr std::size_t kMaxCapturePath = 255;
  std::array<char, kMaxCapturePath + 1> capture_path_{};
  bool capture_pending_ = false;
};

}  // namespace c3::platform
