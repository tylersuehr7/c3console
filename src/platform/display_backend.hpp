// DisplayBackend — the seam between the renderer and whatever owns the screen.
//
// Desktop: DesktopBackend (SDL2 + OpenGL window).
// Target: DrmBackend (GBM + EGL + KMS).
//
// Selected at runtime from CLI flags and config/console.toml. Every feature must work
// under --desktop --source=mock.
//
// Every method is noexcept: this interface is called from the render loop, where errors
// are return values (a null context from begin_frame), never exceptions.
#pragma once

/// NanoVG. The only rendering abstraction. Defined in <nanovg.h>
struct NVGcontext;

namespace c3::platform {

struct Size {
  int width = 0;
  int height = 0;

  [[nodiscard]] constexpr bool operator==(const Size&) const noexcept = default;
};

enum class PresentResult { Presented, Failed };

class DisplayBackend {
public:
  virtual ~DisplayBackend() = default;

  DisplayBackend(const DisplayBackend&) = delete;
  DisplayBackend& operator=(const DisplayBackend&) = delete;
  DisplayBackend(DisplayBackend&&) = delete;
  DisplayBackend& operator=(DisplayBackend&&) = delete;

  /// Panel size in physical pixels. Fixed for the life of the backend.
  [[nodiscard]] virtual Size size() const noexcept = 0;

  /// Pixel density, dots per inch. Used to size type and touch targets.
  [[nodiscard]] virtual float dpi() const noexcept = 0;

  /// The NanoVG context, for creating fonts and images at init, outside any frame.
  /// Never draw with it here; drawing happens between begin_frame() and end_frame().
  [[nodiscard]] virtual NVGcontext* context() const noexcept = 0;

  /// Begin a frame. Returns the NanoVG context to draw into, already inside nvgBeginFrame,
  /// or nullptr if the frame cannot start (context lost, display gone). Must not allocate.
  [[nodiscard]] virtual NVGcontext* begin_frame() noexcept = 0;

  /// End the frame and present it: SDL swap on desktop, DRM pageflip on the target.
  [[nodiscard]] virtual PresentResult end_frame() noexcept = 0;

protected:
  DisplayBackend() = default;
};

}  // namespace c3::platform
