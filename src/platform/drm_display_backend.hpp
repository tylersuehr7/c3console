// DrmDisplayBackend — the target display: KMS mode set, GBM surface, EGL + OpenGL ES 2, NanoVG GLES2
// (spec §5.4 stage 1: gauges drawn by the GPU, no atomic planes).
//
// One connected connector, its preferred mode, page-flipped double buffering. end_frame()
// blocks until the flip completes, so the loop is paced by the panel's refresh exactly as
// the desktop backend is paced by vsync. The panel is drawn through platform/rotation.h so
// scene code sees the logical portrait size regardless of how the LCD scans out. The CRTC
// state found at start is restored on exit. Every DRM, GBM and EGL handle is owned by RAII.
#pragma once

#include "debug_frame_capture.hpp"
#include "display_backend.hpp"
#include "panel_rotation.hpp"
#include "utils/unique_fd.hpp"

#include <xf86drmMode.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

// Sourced from <nanovg.h>
struct gbm_device;
struct gbm_surface;
struct gbm_bo;
struct NVGcontext;

namespace c3::platform {

class DrmDisplayBackend final : public DisplayBackend, public DebugFrameCapture {
public:
  struct Options {
    std::string device = "/dev/dri/card0";
    Rotation rotation = Rotation::R0;
    float fallback_dpi = 264.0F;          // when the connector reports no physical size
    std::optional<Size> prefer_mode;      // native mode to pick when the connector offers several
    std::uint32_t flip_timeout_ms = 100;  // a flip that takes longer is logged and abandoned
  };

  struct Result {
    std::unique_ptr<DrmDisplayBackend> backend;  // null when `error` is set
    std::string error;
  };

  /// Opens the device, picks connector and mode, sets up GBM + EGL + NanoVG. Init-time.
  [[nodiscard]] static Result create(const Options& options);

  ~DrmDisplayBackend() override;
  DrmDisplayBackend(const DrmDisplayBackend&) = delete;
  DrmDisplayBackend& operator=(const DrmDisplayBackend&) = delete;
  DrmDisplayBackend(DrmDisplayBackend&&) = delete;
  DrmDisplayBackend& operator=(DrmDisplayBackend&&) = delete;

  [[nodiscard]] Size size() const noexcept override { return logical_; }
  [[nodiscard]] float dpi() const noexcept override { return dpi_; }
  [[nodiscard]] NVGcontext* context() const noexcept override { return nvg_; }
  [[nodiscard]] NVGcontext* begin_frame() noexcept override;
  [[nodiscard]] PresentResult end_frame() noexcept override;

  [[nodiscard]] bool request_capture(std::string_view ppm_path) noexcept override;

  [[nodiscard]] Size native_size() const noexcept { return native_; }
  [[nodiscard]] Rotation rotation() const noexcept { return rotation_; }
  [[nodiscard]] int fd() const noexcept { return card_.get(); }
  [[nodiscard]] std::uint32_t flips_timed_out() const noexcept { return flips_timed_out_; }

private:
  DrmDisplayBackend() noexcept = default;

  [[nodiscard]] std::string open_device(const Options& options);
  [[nodiscard]] std::string pick_connector_and_mode(const Options& options);
  [[nodiscard]] std::string init_gbm_egl();
  [[nodiscard]] bool wait_for_flip() noexcept;
  [[nodiscard]] std::uint32_t framebuffer_for(gbm_bo* bo) noexcept;

  void write_capture() noexcept;
  void release_all() noexcept;

  struct Framebuffer {
    int fd = -1;
    std::uint32_t id = 0;
  };
  static void destroy_framebuffer(gbm_bo* bo, void* data) noexcept;

private:
  std::array<Framebuffer, 8> framebuffers_{};
  utils::UniqueFd card_;
  std::uint32_t connector_id_ = 0;
  std::uint32_t crtc_id_ = 0;
  std::unique_ptr<drmModeModeInfo> mode_;
  drmModeCrtc* saved_crtc_ = nullptr;  // restored and freed in the destructor
  gbm_device* gbm_ = nullptr;
  gbm_surface* surface_ = nullptr;
  void* egl_display_ = nullptr;
  void* egl_context_ = nullptr;
  void* egl_surface_ = nullptr;
  NVGcontext* nvg_ = nullptr;
  gbm_bo* pending_bo_ = nullptr;
  bool failed_ = false;
  gbm_bo* front_bo_ = nullptr;  // scanned out right now; released after the next flip
  bool mode_set_ = false;
  bool in_frame_ = false;
  bool flip_pending_ = false;
  std::uint32_t flip_timeout_ms_ = 100;
  std::uint32_t flips_timed_out_ = 0;
  Size native_{};
  Size logical_{};
  Rotation rotation_ = Rotation::R0;
  float dpi_ = 264.0F;

  static constexpr std::size_t kMaxCapturePath = 255;
  std::array<char, kMaxCapturePath + 1> capture_path_{};
  bool capture_pending_ = false;
};

}  // namespace c3::platform
