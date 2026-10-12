#include "logging/log.hpp"
#include "nanovg.h"
#include "platform/debug_frame_capture.hpp"
#include "platform/display_backend.hpp"
#include "platform/drm_display_backend.hpp"
#include "platform/panel_rotation.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

using c3::platform::DebugFrameCapture;
using c3::platform::DisplayBackend;
using c3::platform::DrmDisplayBackend;
using c3::platform::Point2;
using c3::platform::PresentResult;
using c3::platform::rotated_size;
using c3::platform::Rotation;
using c3::platform::Size;
using c3::platform::to_native;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;

namespace {

// ---------------------------------------------------------------------------
// Two tiers. The first needs only the DRM libraries and runs in the Bookworm container. The
// second needs a KMS device with a connected panel (the CM4 on the bench or in the car) and
// skips everywhere else; those cases are the automated half of the spec §11 display checks.
// Set C3_DRM_DEVICE to test a card other than /dev/dri/card0.
// ---------------------------------------------------------------------------

void discard_log(void* /*context*/, std::string_view /*line*/) noexcept {}
[[nodiscard]] c3::logging::Sink quiet_sink() noexcept {
  return c3::logging::Sink{.context = nullptr, .emit = &discard_log};
}

[[nodiscard]] std::string rig_device() {
  const char* env = std::getenv("C3_DRM_DEVICE");  // NOLINT(concurrency-mt-unsafe)
  return env != nullptr && *env != '\0' ? std::string(env) : std::string("/dev/dri/card0");
}

/// A fresh scratch directory, removed when the test case ends.
class ScratchDir {
public:
  ScratchDir() {
    static unsigned counter = 0;
    path_ = std::filesystem::temp_directory_path() /
            ("c3console_drm_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
    std::filesystem::remove_all(path_);
    REQUIRE(std::filesystem::create_directory(path_));
  }
  ~ScratchDir() { std::filesystem::remove_all(path_); }
  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;
  ScratchDir(ScratchDir&&) = delete;
  ScratchDir& operator=(ScratchDir&&) = delete;
  [[nodiscard]] std::string file(std::string_view name) const { return (path_ / name).string(); }
  [[nodiscard]] std::string write(std::string_view name, std::string_view content) const {
    const auto path = file(name);
    std::ofstream out(path, std::ios::binary);
    REQUIRE(out.is_open());
    out << content;
    return path;
  }
  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
  std::filesystem::path path_;
};

/// The descriptor number the next open() will return: POSIX hands out the lowest free one.
[[nodiscard]] int next_free_fd() {
  const int probe = ::open("/dev/null", O_RDONLY);
  REQUIRE(probe >= 0);
  REQUIRE(::close(probe) == 0);
  return probe;
}
[[nodiscard]] bool fd_is_open(int fd) noexcept {
  return ::fcntl(fd, F_GETFD) != -1;
}

/// A created backend together with the log silencer that must outlive it.
struct Opened {
  std::unique_ptr<c3::logging::ScopedSink> quiet;
  std::unique_ptr<DrmDisplayBackend> backend;
  [[nodiscard]] DrmDisplayBackend* operator->() const noexcept { return backend.get(); }
  [[nodiscard]] DrmDisplayBackend& operator*() const noexcept { return *backend; }
};

/// Creates on the rig device, or skips when this machine has no usable KMS display.
[[nodiscard]] Opened create_or_skip(DrmDisplayBackend::Options options) {
  Opened opened{.quiet = std::make_unique<c3::logging::ScopedSink>(quiet_sink()), .backend = nullptr};
  if (options.device.empty()) {
    options.device = rig_device();
  }
  auto result = DrmDisplayBackend::create(options);
  if (!result.backend) {
    const std::string& e = result.error;
    const bool no_rig = e.find("cannot open") != std::string::npos ||
                        e.find("drmModeGetResources failed") != std::string::npos ||
                        e.find("no connected connector") != std::string::npos;
    if (no_rig) {
      SKIP("no KMS display on this machine: " << e);
    }
    FAIL("DrmDisplayBackend::create failed on a present display: " << e);
  }
  REQUIRE(result.error.empty());
  opened.backend = std::move(result.backend);
  return opened;
}

[[nodiscard]] Opened create_or_skip() {
  return create_or_skip(DrmDisplayBackend::Options{.device = {}});
}

/// A decoded binary PPM as written by the capture path (native orientation).
struct Ppm {
  int width = 0;
  int height = 0;
  std::string pixels;

  struct Rgb {
    int r;
    int g;
    int b;
  };
  [[nodiscard]] Rgb at(int x, int y) const {
    REQUIRE(x >= 0);
    REQUIRE(y >= 0);
    REQUIRE(x < width);
    REQUIRE(y < height);
    const auto i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 3;
    return {static_cast<unsigned char>(pixels[i]), static_cast<unsigned char>(pixels[i + 1]),
            static_cast<unsigned char>(pixels[i + 2])};
  }
};

[[nodiscard]] Ppm read_ppm(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.is_open());
  const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  Ppm ppm;
  int max = 0;
  int consumed = 0;
  REQUIRE(std::sscanf(content.c_str(), "P6\n%d %d\n%d\n%n", &ppm.width, &ppm.height, &max, &consumed) == 3);
  CHECK(max == 255);
  ppm.pixels = content.substr(static_cast<std::size_t>(consumed));
  REQUIRE(ppm.pixels.size() == static_cast<std::size_t>(ppm.width) * static_cast<std::size_t>(ppm.height) * 3);
  return ppm;
}

[[nodiscard]] bool near_colour(Ppm::Rgb c, int r, int g, int b, int tol = 3) noexcept {
  return std::abs(c.r - r) <= tol && std::abs(c.g - g) <= tol && std::abs(c.b - b) <= tol;
}

/// Paints the top half of the logical panel red and the bottom half blue.
void draw_two_tone(NVGcontext* vg, Size panel) {
  const auto w = static_cast<float>(panel.width);
  const auto h = static_cast<float>(panel.height);
  nvgBeginPath(vg);
  nvgRect(vg, 0.0F, 0.0F, w, h / 2);
  nvgFillColor(vg, nvgRGB(255, 0, 0));
  nvgFill(vg);
  nvgBeginPath(vg);
  nvgRect(vg, 0.0F, h / 2, w, h / 2);
  nvgFillColor(vg, nvgRGB(0, 0, 255));
  nvgFill(vg);
}

void present_two_tone(DrmDisplayBackend& backend) {
  NVGcontext* vg = backend.begin_frame();
  REQUIRE(vg != nullptr);
  draw_two_tone(vg, backend.size());
  REQUIRE(backend.end_frame() == PresentResult::Presented);
}

// ---------------------------------------------------------------------------
// Tier 1: no display required
// ---------------------------------------------------------------------------

TEST_CASE("DrmDisplayBackend: options default to card0, no rotation, the target DPI and a 100 ms flip",
          "[drm_display_backend]") {
  const DrmDisplayBackend::Options defaults{};
  CHECK(defaults.device == "/dev/dri/card0");
  CHECK(defaults.rotation == Rotation::R0);
  CHECK(defaults.fallback_dpi == 264.0F);
  CHECK_FALSE(defaults.prefer_mode.has_value());
  CHECK(defaults.flip_timeout_ms == 100);
}

TEST_CASE("DrmDisplayBackend: is a final, non-copyable DisplayBackend and DebugFrameCapture", "[drm_display_backend]") {
  STATIC_CHECK(std::is_base_of_v<DisplayBackend, DrmDisplayBackend>);
  STATIC_CHECK(std::is_base_of_v<DebugFrameCapture, DrmDisplayBackend>);
  STATIC_CHECK(std::is_final_v<DrmDisplayBackend>);
  STATIC_CHECK_FALSE(std::is_copy_constructible_v<DrmDisplayBackend>);
  STATIC_CHECK_FALSE(std::is_move_constructible_v<DrmDisplayBackend>);
  STATIC_CHECK_FALSE(std::is_default_constructible_v<DrmDisplayBackend>);  // only create() builds one
  STATIC_CHECK(noexcept(std::declval<DrmDisplayBackend&>().begin_frame()));
  STATIC_CHECK(noexcept(std::declval<DrmDisplayBackend&>().end_frame()));
  STATIC_CHECK(noexcept(std::declval<DrmDisplayBackend&>().request_capture(std::string_view{})));
  STATIC_CHECK(noexcept(std::declval<const DrmDisplayBackend&>().size()));
  STATIC_CHECK(noexcept(std::declval<const DrmDisplayBackend&>().native_size()));
  STATIC_CHECK(noexcept(std::declval<const DrmDisplayBackend&>().rotation()));
  STATIC_CHECK(noexcept(std::declval<const DrmDisplayBackend&>().flips_timed_out()));
}

TEST_CASE("create: a missing device is an error naming the path and the reason", "[drm_display_backend]") {
  const c3::logging::ScopedSink quiet(quiet_sink());
  const ScratchDir dir;
  const std::string path = dir.file("card9");
  const auto result = DrmDisplayBackend::create({.device = path});
  CHECK(result.backend == nullptr);
  CHECK_THAT(result.error, StartsWith("cannot open " + path + ": "));
  CHECK_THAT(result.error, ContainsSubstring("No such file"));
}

TEST_CASE("create: a file that is not a KMS device is refused at the resources query", "[drm_display_backend]") {
  const c3::logging::ScopedSink quiet(quiet_sink());
  const ScratchDir dir;
  for (const auto& path : {std::string("/dev/null"), dir.write("plain", "not a card")}) {
    INFO("device: " << path);
    const auto result = DrmDisplayBackend::create({.device = path});
    CHECK(result.backend == nullptr);
    CHECK_THAT(result.error, StartsWith("drmModeGetResources failed"));
    CHECK_THAT(result.error, ContainsSubstring("KMS"));
  }
}

TEST_CASE("create: a directory cannot be opened read-write", "[drm_display_backend]") {
  const c3::logging::ScopedSink quiet(quiet_sink());
  const ScratchDir dir;
  const auto result = DrmDisplayBackend::create({.device = dir.path().string()});
  CHECK(result.backend == nullptr);
  CHECK_THAT(result.error, StartsWith("cannot open "));
}

TEST_CASE("create: a failed create leaks no descriptor", "[drm_display_backend]") {
  const c3::logging::ScopedSink quiet(quiet_sink());
  const int expected_fd = next_free_fd();
  const auto result = DrmDisplayBackend::create({.device = "/dev/null"});
  REQUIRE(result.backend == nullptr);
  CHECK_FALSE(fd_is_open(expected_fd));  // the card descriptor was closed on the error path
}

TEST_CASE("create: the error message carries no trailing newline, ready for the log line", "[drm_display_backend]") {
  const c3::logging::ScopedSink quiet(quiet_sink());
  const auto result = DrmDisplayBackend::create({.device = "/nonexistent-c3console/card0"});
  REQUIRE_FALSE(result.error.empty());
  CHECK(result.error.back() != '\n');
}

// ---------------------------------------------------------------------------
// Tier 2: a KMS device with a connected panel (spec §11 display checks)
// ---------------------------------------------------------------------------

TEST_CASE("rig: create reports a real mode and the panel size follows the rotation", "[drm_display_backend][rig]") {
  const auto backend = create_or_skip();
  const Size native = backend->native_size();
  CHECK(native.width > 0);
  CHECK(native.height > 0);
  CHECK(backend->rotation() == Rotation::R0);
  CHECK(backend->size() == rotated_size(native, Rotation::R0));
  CHECK(backend->dpi() > 0.0F);
  CHECK(backend->context() != nullptr);
  CHECK(backend->fd() >= 0);
  CHECK(backend->flips_timed_out() == 0);
}

TEST_CASE("rig: the descriptor is a KMS device the test can query itself", "[drm_display_backend][rig]") {
  const auto backend = create_or_skip();
  drmModeRes* res = drmModeGetResources(backend->fd());
  REQUIRE(res != nullptr);
  CHECK(res->count_connectors > 0);
  CHECK(res->count_crtcs > 0);
  drmModeFreeResources(res);
}

TEST_CASE("rig: a quarter-turn rotation swaps the logical size but not the native mode", "[drm_display_backend][rig]") {
  Size native{};
  {
    const auto plain = create_or_skip();
    native = plain->native_size();
  }
  const auto turned = create_or_skip(DrmDisplayBackend::Options{.device = {}, .rotation = Rotation::R90});
  CHECK(turned->native_size() == native);
  CHECK(turned->rotation() == Rotation::R90);
  CHECK(turned->size() == Size{.width = native.height, .height = native.width});
}

TEST_CASE("rig: fallback DPI is used only when the connector reports no physical size", "[drm_display_backend][rig]") {
  const auto a = create_or_skip(DrmDisplayBackend::Options{.device = {}, .fallback_dpi = 100.0F});
  const auto b = create_or_skip(DrmDisplayBackend::Options{.device = {}, .fallback_dpi = 200.0F});
  // Either the panel reports its size (both equal, independent of the fallback) or it does not
  // (both equal their fallback). Anything else means the fallback leaked into a measured value.
  const bool measured = a->dpi() == b->dpi();
  const bool fell_back = a->dpi() == 100.0F && b->dpi() == 200.0F;
  CHECK((measured || fell_back));
}

TEST_CASE("rig: the first end_frame sets the mode and later ones page-flip, none timing out",
          "[drm_display_backend][rig]") {
  const auto backend = create_or_skip();
  for (int i = 0; i < 30; ++i) {
    INFO("frame " << i);
    REQUIRE(backend->begin_frame() != nullptr);
    REQUIRE(backend->end_frame() == PresentResult::Presented);
  }
  CHECK(backend->flips_timed_out() == 0);
}

TEST_CASE("rig: begin returns the NanoVG context and a nested begin returns null", "[drm_display_backend][rig]") {
  const auto backend = create_or_skip();
  NVGcontext* vg = backend->begin_frame();
  REQUIRE(vg != nullptr);
  CHECK(vg == backend->context());
  CHECK(backend->begin_frame() == nullptr);
  CHECK(backend->end_frame() == PresentResult::Presented);
}

TEST_CASE("rig: end_frame without a frame fails and stops rendering for good", "[drm_display_backend][rig]") {
  // Spec §5.4: a failed present stops rendering with an error; there is no silent recovery.
  const auto backend = create_or_skip();
  CHECK(backend->end_frame() == PresentResult::Failed);
  CHECK(backend->begin_frame() == nullptr);
  CHECK(backend->end_frame() == PresentResult::Failed);
}

TEST_CASE("rig: capture writes a PPM of the native framebuffer", "[drm_display_backend][rig]") {
  const ScratchDir dir;
  const auto backend = create_or_skip();
  const std::string path = dir.file("frame.ppm");
  REQUIRE(backend->request_capture(path));
  CHECK_FALSE(std::filesystem::exists(path));
  present_two_tone(*backend);
  REQUIRE(std::filesystem::exists(path));
  const Ppm ppm = read_ppm(path);
  CHECK(ppm.width == backend->native_size().width);
  CHECK(ppm.height == backend->native_size().height);
}

TEST_CASE("rig: the drawn panel lands on the glass where the rotation transform says it should",
          "[drm_display_backend][rig]") {
  // Drawing goes through frame_transform(); this samples the scanout buffer at to_native() of
  // known logical points, for every rotation the config allows. Red top half, blue bottom half.
  for (const Rotation r : {Rotation::R0, Rotation::R90, Rotation::R180, Rotation::R270}) {
    const ScratchDir dir;
    const auto backend = create_or_skip(DrmDisplayBackend::Options{.device = {}, .rotation = r});
    const std::string path = dir.file("frame.ppm");
    REQUIRE(backend->request_capture(path));
    present_two_tone(*backend);
    const Ppm ppm = read_ppm(path);

    const Size native = backend->native_size();
    const Size logical = backend->size();
    const auto sample = [&](float lx, float ly) {
      const Point2 n = to_native({.x = lx, .y = ly}, native, r);
      return ppm.at(std::clamp(static_cast<int>(n.x), 0, native.width - 1),
                    std::clamp(static_cast<int>(n.y), 0, native.height - 1));
    };
    const auto lw = static_cast<float>(logical.width);
    const auto lh = static_cast<float>(logical.height);
    INFO("rotation " << c3::platform::to_degrees(r));
    CHECK(near_colour(sample(lw / 2, lh / 4), 255, 0, 0));
    CHECK(near_colour(sample(lw / 2, lh * 3 / 4), 0, 0, 255));
    CHECK(near_colour(sample(4.0F, 4.0F), 255, 0, 0));
    CHECK(near_colour(sample(lw - 5.0F, lh - 5.0F), 0, 0, 255));
  }
}

TEST_CASE("rig: request_capture rules and one pending capture at a time", "[drm_display_backend][rig]") {
  const ScratchDir dir;
  const auto backend = create_or_skip();
  CHECK_FALSE(backend->request_capture(""));
  CHECK_FALSE(backend->request_capture(std::string(256, 'p')));
  CHECK(backend->request_capture(dir.file("a.ppm")));
  CHECK_FALSE(backend->request_capture(dir.file("b.ppm")));
  present_two_tone(*backend);
  CHECK(std::filesystem::exists(dir.file("a.ppm")));
  CHECK_FALSE(std::filesystem::exists(dir.file("b.ppm")));
  CHECK(backend->request_capture(dir.file("b.ppm")));
}

TEST_CASE("rig: an unwritable capture path is reported and the frame still presents", "[drm_display_backend][rig]") {
  const ScratchDir dir;
  const auto backend = create_or_skip();
  REQUIRE(backend->request_capture(dir.file("missing/dir/frame.ppm")));
  REQUIRE(backend->begin_frame() != nullptr);
  CHECK(backend->end_frame() == PresentResult::Presented);
  CHECK_FALSE(std::filesystem::exists(dir.file("missing/dir/frame.ppm")));
  CHECK(backend->request_capture(dir.file("next.ppm")));
}

TEST_CASE("rig: the device can be opened again after the backend is destroyed", "[drm_display_backend][rig]") {
  {
    const auto first = create_or_skip();
    present_two_tone(*first);
  }
  const auto second = create_or_skip();
  present_two_tone(*second);
  CHECK(second->flips_timed_out() == 0);
}

}  // namespace
