#include "background_io.hpp"

#include "settings_store.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <exception>
#include <utility>

namespace c3::app {

BackgroundIO::BackgroundIO(telemetry::CsvRecorder* recorder, std::string settings_path)
    : recorder_(recorder), settings_path_(std::move(settings_path)) {
  update_wall_clock();
  worker_ = std::thread([this] { run(); });
}

BackgroundIO::~BackgroundIO() {
  finish();
}

void BackgroundIO::finish() noexcept {
  stopping_.store(true, std::memory_order_release);
  if (worker_.joinable()) {
    worker_.join();
  }
}

void BackgroundIO::record(const telemetry::Frame& frame) noexcept {
  if (recorder_ != nullptr && !frames_.push(frame)) {
    ++dropped_frames_;
  }
}

logging::Sink BackgroundIO::log_sink() noexcept {
  return {.context = this, .emit = enqueue_log};
}

void BackgroundIO::enqueue_log(void* context, std::string_view text) noexcept {
  auto& self = *static_cast<BackgroundIO*>(context);
  if (std::this_thread::get_id() == self.worker_id_.load(std::memory_order_acquire)) {
    // The worker is the queue's consumer; it must not also produce. Its own lines (a settings
    // error) go straight out, which is where they were headed anyway.
    static_cast<void>(std::fwrite(text.data(), 1, text.size(), stderr));
    return;
  }

  LogLine line;
  line.length = std::min(text.size(), line.text.size());
  std::copy_n(text.data(), line.length, line.text.data());
  if (!self.logs_.push(line)) {
    ++self.dropped_logs_;
  }
}

void BackgroundIO::update_wall_clock() noexcept {
  const auto time = std::time(nullptr);
  std::tm local{};
  if (localtime_r(&time, &local) != nullptr) {
    wall_minutes_.store(static_cast<unsigned>(local.tm_hour * 60 + local.tm_min), std::memory_order_relaxed);
  }
}

// Minutes since midnight -> 12-hour clock; hour 0 and 12 both read "12".
render::WallClock BackgroundIO::wall_clock() const noexcept {
  const auto minutes = wall_minutes_.load(std::memory_order_relaxed);
  const auto hour = minutes / 60;
  return {.hour12 = static_cast<int>(hour % 12 == 0 ? 12 : hour % 12),
          .minute = static_cast<int>(minutes % 60),
          .pm = hour >= 12};
}

void BackgroundIO::run() noexcept {
  worker_id_.store(std::this_thread::get_id(), std::memory_order_release);
  for (;;) {
    // Read the flag first: anything queued before finish() set it is drained below.
    const bool stopping = stopping_.load(std::memory_order_acquire);

    while (const auto frame = frames_.pop()) {
      if (recorder_ != nullptr) {
        recorder_->record(*frame);
      }
    }

    while (const auto line = logs_.pop()) {
      static_cast<void>(std::fwrite(line->text.data(), 1, line->length, stderr));
    }

    render::Settings settings;
    if (settings_.take(settings) && !settings_path_.empty()) {
      try {
        if (const auto error = save_settings(settings_path_, settings)) {
          ++settings_errors_;
          logging::error("settings", *error);
        }
      } catch (const std::exception& error) {
        ++settings_errors_;
        logging::error("settings", error.what());
      }
    }

    if (stopping) {
      return;
    }

    update_wall_clock();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

}  // namespace c3::app
