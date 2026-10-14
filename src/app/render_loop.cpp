#include "render_loop.hpp"

#include "app/background_io.hpp"
#include "app/event_loop.hpp"
#include "app/telemetry_ingestor.hpp"
#include "logging/log.hpp"
#include "render/console_scene.hpp"
#include "render/touch_routing.hpp"
#include "telemetry/snapshot.hpp"
#include "utils/clock.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>

namespace c3::app {

namespace {

constexpr std::string_view kUnit = "render";
constexpr std::chrono::microseconds kTickPeriod{16667};  // 60 Hz, spec §5.3

// Rolling frame-time statistics, logged every few seconds.
// cpu includes ingestion, input and drawing, before the presentation wait.
// frame includes the swap wait and tracks the refresh period on a vsynced
// desktop. The spec's 8 ms budget is the cpu figure.
class FrameStats {
public:
  void add(double cpu_ms, double frame_ms) noexcept {
    ++count_;
    cpu_sum_ms_ += cpu_ms;
    cpu_max_ms_ = std::max(cpu_max_ms_, cpu_ms);
    frame_sum_ms_ += frame_ms;
  }

  void flush(std::uint64_t now_ms) noexcept {
    if (now_ms - last_flush_ms_ < kPeriodMs || count_ == 0) {
      return;
    }

    const auto n = static_cast<double>(count_);
    logging::info(kUnit, "frames=", count_, " cpu avg=", cpu_sum_ms_ / n, " ms max=", cpu_max_ms_,
                  " ms, frame avg=", frame_sum_ms_ / n, " ms");

    count_ = 0;
    cpu_sum_ms_ = 0.0;
    cpu_max_ms_ = 0.0;
    frame_sum_ms_ = 0.0;
    last_flush_ms_ = now_ms;
  }

private:
  static constexpr std::uint64_t kPeriodMs = 5000;
  std::uint64_t last_flush_ms_ = 0;
  std::uint64_t count_ = 0;
  double cpu_sum_ms_ = 0.0;
  double cpu_max_ms_ = 0.0;
  double frame_sum_ms_ = 0.0;
};

// Render side. Everything the tick needs, so the lambda handed to the loop stays a call.
class RenderTick {
public:
  RenderTick(platform::DisplayBackend& display, platform::InputSource& input, render::ConsoleScene& scene,
             DockController& controls, WellController& surfaces, const RenderLoopOptions& opts,
             TelemetryIngestor& ingestion, BackgroundIO& io) noexcept
      : display_(display),
        input_(input),
        scene_(scene),
        controls_(controls),
        surfaces_(surfaces),
        opts_(opts),
        ingestion_(ingestion),
        io_(io),
        start_(Clock::now()),
        last_tick_(start_) {}

  [[nodiscard]] LoopControl operator()() noexcept {
    if (input_.quit_requested()) {
      return LoopControl::Stop;
    }

    const auto tick_start = Clock::now();
    ingestion_.pump([this](const telemetry::Frame& frame) noexcept { io_.record(frame); });

    const bool frame_changed = ingestion_.update();
    const auto telemetry = ingestion_.view(opts_.stall_timeout_ms);
    const std::uint64_t now_ms = utils::steady_now_ms();
    controls_.set_live(telemetry.live());
    if (frame_changed && telemetry.frame != nullptr) {
      controls_.apply_frame(*telemetry.frame);
    }

    surfaces_.tick(now_ms);
    if (telemetry.frame != nullptr) {
      surfaces_.apply_telemetry(*telemetry.frame, telemetry.live(), now_ms);
    }

    drain_input();

    if (const auto theme = surfaces_.take_theme_change()) {
      scene_.set_theme(*theme);
    }

    if (const auto changed = surfaces_.take_settings_change(); changed && !opts_.settings_path.empty()) {
      io_.save(*changed);
    }

    const auto t0 = Clock::now();
    NVGcontext* vg = display_.begin_frame();
    if (vg == nullptr) {
      logging::error(kUnit, "begin_frame failed; stopping");
      exit_code_ = 1;
      return LoopControl::Stop;
    }

    const render::WellView view = surfaces_.view(telemetry.frame, telemetry.live(), telemetry.link);
    const render::SceneInput scene_in{
        .telemetry = telemetry,
        .carplay_connected = false,
        .dock = controls_.model(),
        .clock = io_.wall_clock(),
        .dt_s = std::chrono::duration<float>(t0 - last_tick_).count(),
        .t_s = std::chrono::duration<float>(t0 - start_).count(),
        .units = surfaces_.settings().units,
        .well = view,
    };
    last_tick_ = t0;
    scene_.update(scene_in);
    scene_.draw(vg, scene_in);

    const bool capture_now =
        opts_.screenshot_path && opts_.capture != nullptr && scene_in.t_s >= opts_.screenshot_after_s;
    if (capture_now && !opts_.capture->request_capture(*opts_.screenshot_path)) {
      logging::error(kUnit, "capture request refused for ", *opts_.screenshot_path);
    }

    const auto t_drawn = Clock::now();
    if (display_.end_frame() != platform::PresentResult::Presented) {
      logging::error(kUnit, "presentation failed; stopping");
      exit_code_ = 1;
      return LoopControl::Stop;
    }

    const auto t_end = Clock::now();
    stats_.add(std::chrono::duration<double, std::milli>(t_drawn - tick_start).count(),
               std::chrono::duration<double, std::milli>(t_end - tick_start).count());
    ++frames_;

    if (capture_now) {
      logging::info(kUnit, "screenshot taken after ", frames_, " frames; exiting");
      return LoopControl::Stop;
    }

    stats_.flush(now_ms);
    return LoopControl::Continue;
  }

  [[nodiscard]] int exit_code() const noexcept { return exit_code_; }
  [[nodiscard]] std::uint64_t frames() const noexcept { return frames_; }

private:
  using Clock = std::chrono::steady_clock;

  void dispatch(const render::RoutedGesture& routed) noexcept {
    const auto& event = routed.event;
    if (routed.target == render::TouchTarget::Well) {
      // The dongle bridge is not implemented yet; preserve complete gestures at its seam.
      logging::debug(kUnit, "dongle touch ", platform::to_string(event.phase), " (", event.x, ", ", event.y, ")");
    } else if (event.phase == platform::TouchPhase::Cancel) {
      static_cast<void>(surfaces_.on_touch(event));
      controls_.on_touch(event, scene_.dock_layout());
    } else if (!surfaces_.on_touch(event)) {
      controls_.on_touch(event, scene_.dock_layout());
    }
  }

  void sync_touch_surface() noexcept {
    const unsigned surface = surfaces_.camera_showing() ? static_cast<unsigned>(render::kPageCount)
                                                        : static_cast<unsigned>(surfaces_.page());
    if (const auto cancelled = touch_router_.set_surface(surface)) {
      dispatch(*cancelled);
    }
  }

  void drain_input() noexcept {
    sync_touch_surface();
    platform::TouchEvent event;
    for (unsigned count = 0; count < 64 && input_.poll(event); ++count) {
      if (const auto routed = touch_router_.route(scene_.layout(), event, surfaces_.carplay_active())) {
        dispatch(*routed);
      }
      sync_touch_surface();
    }
  }

private:
  render::TouchRouter touch_router_;
  platform::DisplayBackend& display_;
  platform::InputSource& input_;
  render::ConsoleScene& scene_;
  DockController& controls_;
  WellController& surfaces_;
  const RenderLoopOptions& opts_;
  TelemetryIngestor& ingestion_;
  BackgroundIO& io_;
  Clock::time_point start_;
  Clock::time_point last_tick_;
  FrameStats stats_;
  std::uint64_t frames_ = 0;
  int exit_code_ = 0;
};

}  // namespace

int run_render_loop(platform::DisplayBackend& display, platform::InputSource& input, telemetry::SensorSource& source,
                    render::ConsoleScene& scene, DockController& controls, WellController& surfaces,
                    const RenderLoopOptions& opts, const std::atomic<bool>& stop) {
  TelemetryIngestor ingestion(source);
  BackgroundIO io(opts.recorder, opts.settings_path);
  RenderTick tick(display, input, scene, controls, surfaces, opts, ingestion, io);

  EventLoop loop({.tick_period = kTickPeriod, .watchdog = opts.watchdog});
  if (source.descriptor() >= 0) {
    loop.add_readable(source.descriptor(),
                      [&] { ingestion.pump([&](const telemetry::Frame& frame) noexcept { io.record(frame); }); });
  }
  loop.set_tick([&] { return tick(); });

  logging::info(kUnit, "render loop started; panel ", display.size().width, "x", display.size().height,
                opts.screenshot_path ? ", screenshot mode" : "",
                source.descriptor() >= 0 ? ", telemetry on fd" : ", telemetry polled on tick");

  {
    logging::ScopedSink log_sink(io.log_sink());
    loop.run(stop);
  }

  io.finish();
  if (io.dropped_frames() != 0 || io.dropped_logs() != 0) {
    logging::warn(kUnit, "I/O queue overflow: dropped frames=", io.dropped_frames(), " logs=", io.dropped_logs());
  }

  logging::info(kUnit, "render loop stopped after ", tick.frames(), " frames");
  if (opts.recorder != nullptr) {
    logging::info(kUnit, "recorded ", opts.recorder->frames_written(), " frames to ", opts.recorder->path());
    if (opts.recorder->write_errors() > 0) {
      logging::error(kUnit, opts.recorder->write_errors(), " frames could not be written to ", opts.recorder->path());
    }
  }

  return tick.exit_code();
}

}  // namespace c3::app
