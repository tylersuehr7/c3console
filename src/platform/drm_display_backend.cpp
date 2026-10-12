#include "drm_display_backend.hpp"

#include "logging/log.hpp"
#include "nanovg.h"
#include "platform/nvg_gl.hpp"
#include "utils/clock.hpp"

// GBM-only program: keep X11 out of the EGL platform types (EGLNativeDisplayType becomes void*).
#define EGL_NO_X11 1               // NOLINT(cppcoreguidelines-macro-usage)
#define MESA_EGL_NO_X11_HEADERS 1  // NOLINT(cppcoreguidelines-macro-usage) older Mesa spelling
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <fcntl.h>
#include <gbm.h>
#include <poll.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <system_error>
#include <vector>

namespace c3::platform {

namespace {
constexpr std::string_view kUnit = "drm";
constexpr std::uint32_t kFormat = GBM_FORMAT_XRGB8888;

[[nodiscard]] std::string errno_text() {
  return std::system_category().message(errno);
}

void on_page_flip(int /*fd*/, unsigned /*sequence*/, unsigned /*sec*/, unsigned /*usec*/, void* user_data) {
  *static_cast<bool*>(user_data) = false;  // flip_pending_ cleared
}

struct FileCloser {
  void operator()(std::FILE* f) const noexcept {
    static_cast<void>(std::fclose(f));  // NOLINT(cppcoreguidelines-owning-memory): the deleter owns it
  }
};
using FilePtr = std::unique_ptr<std::FILE, FileCloser>;

struct ConnectorDeleter {
  void operator()(drmModeConnector* c) const noexcept { drmModeFreeConnector(c); }
};
struct EncoderDeleter {
  void operator()(drmModeEncoder* e) const noexcept { drmModeFreeEncoder(e); }
};
struct ResourcesDeleter {
  void operator()(drmModeRes* r) const noexcept { drmModeFreeResources(r); }
};

// Picks the mode matching `prefer` (native or rotated), else the preferred one, else the first.
[[nodiscard]] const drmModeModeInfo* choose_mode(const drmModeConnector& conn,
                                                 const std::optional<Size>& prefer) noexcept {
  const drmModeModeInfo* preferred = nullptr;

  for (int i = 0; i < conn.count_modes; ++i) {
    const drmModeModeInfo& m = conn.modes[i];  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)

    if (prefer && ((m.hdisplay == prefer->width && m.vdisplay == prefer->height) ||
                   (m.hdisplay == prefer->height && m.vdisplay == prefer->width))) {
      return &m;
    }

    if (preferred == nullptr && (m.type & DRM_MODE_TYPE_PREFERRED) != 0) {
      preferred = &m;
    }
  }

  if (preferred != nullptr) {
    return preferred;
  }

  return conn.count_modes > 0 ? &conn.modes[0] : nullptr;
}

}  // namespace

DrmDisplayBackend::Result DrmDisplayBackend::create(const Options& options) {
  std::unique_ptr<DrmDisplayBackend> b(new DrmDisplayBackend());
  b->rotation_ = options.rotation;
  b->flip_timeout_ms_ = options.flip_timeout_ms;

  for (const auto step : {&DrmDisplayBackend::open_device, &DrmDisplayBackend::pick_connector_and_mode}) {
    if (std::string err = (b.get()->*step)(options); !err.empty()) {
      return {nullptr, std::move(err)};
    }
  }

  if (std::string err = b->init_gbm_egl(); !err.empty()) {
    return {nullptr, std::move(err)};
  }

  logging::info(kUnit, "drm backend: ", options.device, " connector ", b->connector_id_, " crtc ", b->crtc_id_,
                " mode ", b->native_.width, "x", b->native_.height, "@", b->mode_->vrefresh, " rotation ",
                to_degrees(b->rotation_), " -> panel ", b->logical_.width, "x", b->logical_.height, " @ ", b->dpi_,
                " dpi");

  return {std::move(b), {}};
}

DrmDisplayBackend::~DrmDisplayBackend() {
  release_all();
}

void DrmDisplayBackend::release_all() noexcept {
  if (mode_set_ && card_) {
    const bool restored = saved_crtc_ != nullptr && saved_crtc_->mode_valid != 0 &&
                          drmModeSetCrtc(card_.get(), saved_crtc_->crtc_id, saved_crtc_->buffer_id, saved_crtc_->x,
                                         saved_crtc_->y, &connector_id_, 1, &saved_crtc_->mode) == 0;
    if (!restored && drmModeSetCrtc(card_.get(), crtc_id_, 0, 0, 0, nullptr, 0, nullptr) != 0) {
      // A pending flip may still reference both buffers. Let process teardown close KMS;
      // returning either buffer to GBM here could let it be reused during scanout.
      logging::error(kUnit, "cannot detach scanout; exiting with buffers retained");
      std::_Exit(EXIT_FAILURE);
    }
  }

  if (saved_crtc_ != nullptr) {
    drmModeFreeCrtc(saved_crtc_);
    saved_crtc_ = nullptr;
  }

  if (nvg_ != nullptr) {
    nvgDeleteGLES2(nvg_);
    nvg_ = nullptr;
  }

  if (egl_display_ != nullptr) {
    eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (egl_surface_ != nullptr) {
      eglDestroySurface(egl_display_, egl_surface_);
      egl_surface_ = nullptr;
    }
    if (egl_context_ != nullptr) {
      eglDestroyContext(egl_display_, egl_context_);
      egl_context_ = nullptr;
    }
    eglTerminate(egl_display_);
    egl_display_ = nullptr;
  }

  if (surface_ != nullptr) {
    if (pending_bo_ != nullptr) {
      gbm_surface_release_buffer(surface_, pending_bo_);
      pending_bo_ = nullptr;
    }
    if (front_bo_ != nullptr) {
      gbm_surface_release_buffer(surface_, front_bo_);
      front_bo_ = nullptr;
    }
    gbm_surface_destroy(surface_);
    surface_ = nullptr;
  }

  if (gbm_ != nullptr) {
    gbm_device_destroy(gbm_);
    gbm_ = nullptr;
  }

  card_.reset();
}

std::string DrmDisplayBackend::open_device(const Options& options) {
  card_.reset(::open(options.device.c_str(), O_RDWR | O_CLOEXEC));
  if (!card_) {
    return "cannot open " + options.device + ": " + errno_text();
  }
  return {};
}

std::string DrmDisplayBackend::pick_connector_and_mode(const Options& options) {
  const std::unique_ptr<drmModeRes, ResourcesDeleter> res(drmModeGetResources(card_.get()));
  if (!res) {
    return "drmModeGetResources failed: " + errno_text() + " (is this a KMS device?)";
  }

  std::unique_ptr<drmModeConnector, ConnectorDeleter> conn;
  for (int i = 0; i < res->count_connectors && !conn; ++i) {
    std::unique_ptr<drmModeConnector, ConnectorDeleter> c(
        drmModeGetConnector(card_.get(), res->connectors[i]));  // NOLINT
    if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
      conn = std::move(c);
    }
  }

  if (!conn) {
    return "no connected connector with modes (panel cable, or a driver still probing)";
  }

  const drmModeModeInfo* mode = choose_mode(*conn, options.prefer_mode);
  if (mode == nullptr) {
    return "connector has no usable mode";
  }

  mode_ = std::make_unique<drmModeModeInfo>(*mode);
  connector_id_ = conn->connector_id;

  // The CRTC currently driving this connector, or the first one its encoder can use.
  if (conn->encoder_id != 0) {
    const std::unique_ptr<drmModeEncoder, EncoderDeleter> enc(drmModeGetEncoder(card_.get(), conn->encoder_id));
    if (enc && enc->crtc_id != 0) {
      crtc_id_ = enc->crtc_id;
    }
  }

  for (int i = 0; crtc_id_ == 0 && i < conn->count_encoders; ++i) {
    const std::unique_ptr<drmModeEncoder, EncoderDeleter> enc(
        drmModeGetEncoder(card_.get(), conn->encoders[i]));  // NOLINT

    if (!enc) {
      continue;
    }

    for (int c = 0; c < res->count_crtcs; ++c) {
      if ((enc->possible_crtcs & (1U << static_cast<unsigned>(c))) != 0) {
        crtc_id_ = res->crtcs[c];  // NOLINT
        break;
      }
    }
  }

  if (crtc_id_ == 0) {
    return "no CRTC can drive the connector";
  }
  saved_crtc_ = drmModeGetCrtc(card_.get(), crtc_id_);

  native_ = {.width = mode_->hdisplay, .height = mode_->vdisplay};
  logical_ = rotated_size(native_, rotation_);

  if (conn->mmWidth > 0) {
    dpi_ = static_cast<float>(mode_->hdisplay) / (static_cast<float>(conn->mmWidth) / 25.4F);
  } else {
    dpi_ = options.fallback_dpi;
  }

  return {};
}

std::string DrmDisplayBackend::init_gbm_egl() {
  gbm_ = gbm_create_device(card_.get());
  if (gbm_ == nullptr) {
    return "gbm_create_device failed";
  }

  surface_ =
      gbm_surface_create(gbm_, static_cast<std::uint32_t>(native_.width), static_cast<std::uint32_t>(native_.height),
                         kFormat, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);

  if (surface_ == nullptr) {
    return "gbm_surface_create failed (XRGB8888 scanout not supported?)";
  }

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): EGL's documented casting
  auto get_platform_display =
      reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));

  egl_display_ = get_platform_display != nullptr ? get_platform_display(EGL_PLATFORM_GBM_KHR, gbm_, nullptr)
                                                 : eglGetDisplay(static_cast<EGLNativeDisplayType>(gbm_));
  if (egl_display_ == EGL_NO_DISPLAY) {
    return "no EGL display for the GBM device";
  }

  EGLint major = 0;
  EGLint minor = 0;

  if (eglInitialize(egl_display_, &major, &minor) != EGL_TRUE) {
    return "eglInitialize failed";
  }

  if (eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE) {
    return "eglBindAPI(GLES) failed";
  }

  // A GLES2 window config whose native visual is our GBM format, with the stencil NanoVG needs.
  const std::array<EGLint, 15> attribs = {EGL_SURFACE_TYPE,
                                          EGL_WINDOW_BIT,
                                          EGL_RED_SIZE,
                                          8,
                                          EGL_GREEN_SIZE,
                                          8,
                                          EGL_BLUE_SIZE,
                                          8,
                                          EGL_ALPHA_SIZE,
                                          0,
                                          EGL_STENCIL_SIZE,
                                          8,
                                          EGL_RENDERABLE_TYPE,
                                          EGL_OPENGL_ES2_BIT,
                                          EGL_NONE};

  EGLint count = 0;
  if (eglChooseConfig(egl_display_, attribs.data(), nullptr, 0, &count) != EGL_TRUE || count <= 0) {
    return "no EGL config with GLES2 + 8-bit stencil";
  }

  std::vector<EGLConfig> configs(static_cast<std::size_t>(count));
  eglChooseConfig(egl_display_, attribs.data(), configs.data(), count, &count);
  EGLConfig config = nullptr;

  for (EGLint i = 0; i < count; ++i) {
    EGLint visual = 0;
    if (eglGetConfigAttrib(egl_display_, configs[static_cast<std::size_t>(i)], EGL_NATIVE_VISUAL_ID, &visual) ==
            EGL_TRUE &&
        static_cast<std::uint32_t>(visual) == kFormat) {
      config = configs[static_cast<std::size_t>(i)];
      break;
    }
  }

  if (config == nullptr) {
    config = configs[0];  // Mesa usually lists XRGB first anyway; log and try
    logging::warn(kUnit, "no EGL config advertises XRGB8888; using the first match");
  }

  const std::array<EGLint, 3> ctx_attribs = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  egl_context_ = eglCreateContext(egl_display_, config, EGL_NO_CONTEXT, ctx_attribs.data());
  if (egl_context_ == EGL_NO_CONTEXT) {
    return "eglCreateContext failed";
  }

  egl_surface_ = eglCreateWindowSurface(egl_display_, config,
                                        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                                        reinterpret_cast<EGLNativeWindowType>(surface_), nullptr);

  if (egl_surface_ == EGL_NO_SURFACE) {
    return "eglCreateWindowSurface failed";
  }

  if (eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, egl_context_) != EGL_TRUE) {
    return "eglMakeCurrent failed";
  }

  nvg_ = nvgCreateGLES2(nvg::kAntialias | nvg::kStencilStrokes);
  if (nvg_ == nullptr) {
    return "nvgCreateGLES2 failed";
  }

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): GL returns const GLubyte*
  const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
  logging::info(kUnit, "EGL ", major, ".", minor, ", ", version != nullptr ? version : "GLES ?");

  return {};
}

NVGcontext* DrmDisplayBackend::begin_frame() noexcept {
  if (failed_ || in_frame_ || nvg_ == nullptr) {
    return nullptr;
  }

  glViewport(0, 0, native_.width, native_.height);
  glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
  glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  nvgBeginFrame(nvg_, static_cast<float>(native_.width), static_cast<float>(native_.height), 1.0F);

  const FrameTransform t = frame_transform(native_, rotation_);
  nvgTranslate(nvg_, t.translate_x, t.translate_y);
  nvgRotate(nvg_, t.angle_rad);
  in_frame_ = true;

  return nvg_;
}

std::uint32_t DrmDisplayBackend::framebuffer_for(gbm_bo* bo) noexcept {
  if (auto* cached = static_cast<Framebuffer*>(gbm_bo_get_user_data(bo)); cached != nullptr) {
    return cached->id;
  }

  auto* slot =
      std::find_if(framebuffers_.begin(), framebuffers_.end(), [](const auto& fb) noexcept { return fb.id == 0; });

  if (slot == framebuffers_.end()) {
    logging::error(kUnit, "framebuffer cache capacity exceeded");
    return 0;
  }

  const std::uint32_t w = gbm_bo_get_width(bo);
  const std::uint32_t h = gbm_bo_get_height(bo);
  std::array<std::uint32_t, 4> handles = {gbm_bo_get_handle(bo).u32, 0, 0, 0};
  std::array<std::uint32_t, 4> strides = {gbm_bo_get_stride(bo), 0, 0, 0};
  std::array<std::uint32_t, 4> offsets = {0, 0, 0, 0};
  std::uint32_t fb_id = 0;

  if (drmModeAddFB2(card_.get(), w, h, kFormat, handles.data(), strides.data(), offsets.data(), &fb_id, 0) != 0 &&
      drmModeAddFB(card_.get(), w, h, 24, 32, strides[0], handles[0], &fb_id) != 0) {
    logging::error(kUnit, "drmModeAddFB failed: ", errno);
    return 0;
  }

  *slot = {.fd = card_.get(), .id = fb_id};
  gbm_bo_set_user_data(bo, &*slot, destroy_framebuffer);

  return fb_id;
}

void DrmDisplayBackend::destroy_framebuffer(gbm_bo* /*bo*/, void* data) noexcept {
  auto& framebuffer = *static_cast<Framebuffer*>(data);
  static_cast<void>(drmModeRmFB(framebuffer.fd, framebuffer.id));
  framebuffer = {};
}

bool DrmDisplayBackend::wait_for_flip() noexcept {
  drmEventContext event{};
  event.version = 2;
  event.page_flip_handler = on_page_flip;
  const auto deadline = utils::steady_now_ms() + flip_timeout_ms_;

  while (flip_pending_) {
    const auto now = utils::steady_now_ms();
    if (now >= deadline) {
      ++flips_timed_out_;
      return false;
    }

    pollfd descriptor{.fd = card_.get(), .events = POLLIN, .revents = 0};

    const int ready = ::poll(&descriptor, 1, static_cast<int>(deadline - now));
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }

    if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
      return false;
    }

    if ((descriptor.revents & POLLIN) != 0 && drmHandleEvent(card_.get(), &event) != 0) {
      return false;
    }
  }

  return true;
}

PresentResult DrmDisplayBackend::end_frame() noexcept {
  if (!in_frame_) {
    failed_ = true;
    return PresentResult::Failed;
  }

  nvgEndFrame(nvg_);
  if (capture_pending_) {
    write_capture();
  }

  in_frame_ = false;
  if (eglSwapBuffers(egl_display_, egl_surface_) != EGL_TRUE) {
    logging::error(kUnit, "eglSwapBuffers failed");
    failed_ = true;
    return PresentResult::Failed;
  }

  gbm_bo* bo = gbm_surface_lock_front_buffer(surface_);
  if (bo == nullptr) {
    logging::error(kUnit, "gbm_surface_lock_front_buffer returned null");
    failed_ = true;
    return PresentResult::Failed;
  }

  const std::uint32_t fb = framebuffer_for(bo);
  if (fb == 0) {
    gbm_surface_release_buffer(surface_, bo);
    failed_ = true;
    return PresentResult::Failed;
  }

  if (!mode_set_) {
    if (drmModeSetCrtc(card_.get(), crtc_id_, fb, 0, 0, &connector_id_, 1, mode_.get()) != 0) {
      logging::error(kUnit, "drmModeSetCrtc failed: ", errno);
      gbm_surface_release_buffer(surface_, bo);
      failed_ = true;
      return PresentResult::Failed;
    }
    mode_set_ = true;
  } else {
    flip_pending_ = true;
    if (drmModePageFlip(card_.get(), crtc_id_, fb, DRM_MODE_PAGE_FLIP_EVENT, &flip_pending_) != 0) {
      logging::error(kUnit, "drmModePageFlip failed: ", errno);
      flip_pending_ = false;
      gbm_surface_release_buffer(surface_, bo);
      failed_ = true;
      return PresentResult::Failed;
    }

    pending_bo_ = bo;
    if (!wait_for_flip()) {
      // A timed-out flip may still complete. Neither buffer is reusable until KMS detaches.
      failed_ = true;
      return PresentResult::Failed;
    }
    pending_bo_ = nullptr;
  }

  // The buffer that was on screen is free once the new one is scanning out.
  if (front_bo_ != nullptr) {
    gbm_surface_release_buffer(surface_, front_bo_);
  }

  front_bo_ = bo;
  return PresentResult::Presented;
}

bool DrmDisplayBackend::request_capture(std::string_view ppm_path) noexcept {
  if (capture_pending_ || ppm_path.empty() || ppm_path.size() > kMaxCapturePath) {
    return false;
  }

  std::copy(ppm_path.begin(), ppm_path.end(), capture_path_.begin());
  capture_path_[ppm_path.size()] = '\0';
  capture_pending_ = true;

  return true;
}

void DrmDisplayBackend::write_capture() noexcept {
  capture_pending_ = false;
  const int w = native_.width;
  const int h = native_.height;
  const std::size_t row = static_cast<std::size_t>(w) * 4;

  std::vector<unsigned char> pixels;
  try {
    pixels.resize(row * static_cast<std::size_t>(h));
  } catch (...) {
    logging::error(kUnit, "capture: out of memory");
    return;
  }

  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());  // GLES2: RGBA only

  const FilePtr f(std::fopen(capture_path_.data(), "wb"));
  if (!f) {
    logging::error(kUnit, "capture: cannot open ", capture_path_.data());
    return;
  }

  static_cast<void>(std::fprintf(f.get(), "P6\n%d %d\n255\n", w, h));  // NOLINT
  for (int y = h - 1; y >= 0; --y) {
    const std::span<const unsigned char> line(&pixels[static_cast<std::size_t>(y) * row], row);
    for (int x = 0; x < w; ++x) {
      static_cast<void>(std::fwrite(line.subspan(static_cast<std::size_t>(x) * 4, 3).data(), 1, 3, f.get()));
    }
  }

  logging::info(kUnit, "capture written: ", capture_path_.data(), " (", w, "x", h, ", native orientation)");
}

}  // namespace c3::platform
