// BackgroundIO — the one worker thread that does the blocking I/O the frame path must not:
// appending frames to the CSV recorder, writing log lines to stderr, persisting settings, and
// reading the wall clock. The render thread hands work over through lock-free queues and
// never waits; when a queue is full the item is dropped and counted rather than stalling a
// frame (recording and logging are conveniences, the display is not).
//
// Threads: the owning thread is the single producer of every queue (record(), save() and the
// log sink must all be called from it); the worker is the single consumer. A log line emitted
// on the worker itself (a settings error) is written straight to stderr instead of queued, so
// the worker never becomes a second producer. Queued items are drained before finish() returns.
#pragma once

#include "logging/log.hpp"
#include "render/console_scene.hpp"
#include "render/settings.hpp"
#include "telemetry/csv_recorder.hpp"
#include "utils/spsc_queue.hpp"
#include "utils/triple_buffer.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <string>
#include <string_view>
#include <thread>

namespace c3::app {

class BackgroundIO {
public:
  static constexpr std::size_t kFrameQueueDepth = 256;  // 25 s of frames at 10 Hz
  static constexpr std::size_t kLogQueueDepth = 128;
  static constexpr std::size_t kMaxLogLine = 1024;  // longer lines are truncated, never split

  /// Starts the worker. `recorder` may be null (nothing recorded; record() is then free);
  /// `settings_path` may be empty (save() then discards). Both must outlive the object.
  explicit BackgroundIO(telemetry::CsvRecorder* recorder, std::string settings_path = {});

  ~BackgroundIO();
  BackgroundIO(const BackgroundIO&) = delete;
  BackgroundIO& operator=(const BackgroundIO&) = delete;
  BackgroundIO(BackgroundIO&&) = delete;
  BackgroundIO& operator=(BackgroundIO&&) = delete;

  /// Queues a frame for the recorder. Drops and counts it when the queue is full.
  void record(const telemetry::Frame& frame) noexcept;

  /// Queues the settings for an atomic write to `settings_path`. Only the newest unsaved value
  /// is kept: a burst of taps writes the file once.
  void save(const render::Settings& settings) noexcept { settings_.publish(settings); }

  /// Drains what is queued and stops the worker. Idempotent; the destructor calls it. The
  /// counters below are final once it returns.
  void finish() noexcept;

  /// A logging::Sink that queues lines for the worker to write to stderr. Install it with
  /// logging::ScopedSink for the duration of the loop, and only on the owning thread.
  [[nodiscard]] logging::Sink log_sink() noexcept;

  /// Local time of day, refreshed by the worker; the frame path never calls localtime.
  [[nodiscard]] render::WallClock wall_clock() const noexcept;

  [[nodiscard]] unsigned dropped_frames() const noexcept { return dropped_frames_; }
  [[nodiscard]] unsigned dropped_logs() const noexcept { return dropped_logs_; }
  [[nodiscard]] unsigned settings_errors() const noexcept { return settings_errors_.load(); }

private:
  struct LogLine {
    std::array<char, kMaxLogLine> text{};
    std::size_t length = 0;
  };

  static void enqueue_log(void* context, std::string_view text) noexcept;
  void run() noexcept;
  void update_wall_clock() noexcept;

private:
  telemetry::CsvRecorder* recorder_;
  std::string settings_path_;
  utils::SpscQueue<telemetry::Frame, kFrameQueueDepth> frames_;
  utils::SpscQueue<LogLine, kLogQueueDepth> logs_;
  utils::TripleBuffer<render::Settings> settings_;
  unsigned dropped_frames_ = 0;  // producer thread only
  unsigned dropped_logs_ = 0;    // producer thread only
  std::atomic<unsigned> settings_errors_{0};
  std::atomic<unsigned> wall_minutes_{0};  // minutes since local midnight
  std::atomic<bool> stopping_{false};
  std::atomic<std::thread::id> worker_id_{};  // set by the worker as it starts
  std::thread worker_;
};

}  // namespace c3::app
