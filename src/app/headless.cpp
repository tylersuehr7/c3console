#include "headless.hpp"

#include "app/background_io.hpp"
#include "app/event_loop.hpp"
#include "app/telemetry_ingestor.hpp"
#include "logging/log.hpp"
#include "telemetry/fault_codes.hpp"
#include "utils/clock.hpp"

#include <array>
#include <chrono>

namespace c3::app {

namespace {

using namespace std::chrono_literals;

constexpr std::string_view kUnit = "headless";
constexpr std::chrono::microseconds kTickPeriod = 100ms;  // the MCU frame rate
constexpr auto kLogInterval = 1s;
constexpr std::uint32_t kStallTimeoutMs = 1000;  // spec §4.3; the summary says "stalled" past it

// One line per second: the latest frame's values and faults, or the link state when stale.
void log_summary(const telemetry::Frame& frame, const telemetry::TelemetryView& view) noexcept {
  utils::FixedText<256> faults;
  std::array<char, telemetry::kMaxFaultCodeLength> buffer{};

  for (const auto& fault : frame.faults) {
    if (!faults.view().empty()) {
      faults.append(',');
    }
    const auto length = telemetry::format_fault_code(fault, buffer);
    faults.append(std::string_view(buffer.data(), length));
  }

  if (!view.live()) {
    logging::warn(kUnit, "telemetry ", to_string(view.link), "; last frame age=", view.age_ms, " ms");
    return;
  }

  logging::info(kUnit, "t=", frame.t_ms, " ms fuel=", frame.fuel_fraction * 100.0F, "% oil=", frame.oil_psi,
                " psi coolant=", frame.coolant_f, " F voltage=", frame.voltage_v, " V faults=", faults.view());
}

}  // namespace

int run_headless(telemetry::SensorSource& source, std::uint32_t seconds, const std::atomic<bool>& stop,
                 telemetry::CsvRecorder* recorder) {
  using Clock = std::chrono::steady_clock;
  const auto start = Clock::now();
  const auto deadline = seconds == 0 ? Clock::time_point::max() : start + std::chrono::seconds(seconds);

  if (seconds == 0) {
    logging::info(kUnit, "running until interrupted");
  } else {
    logging::info(kUnit, "running for ", seconds, " s");
  }

  TelemetryIngestor ingestion(source);
  BackgroundIO io(recorder);
  const auto pump = [&]() noexcept {
    ingestion.pump([&](const telemetry::Frame& frame) noexcept { io.record(frame); });
  };
  bool have_frame = false;
  auto next_log = start;

  EventLoop loop({.tick_period = kTickPeriod, .watchdog = nullptr});
  if (source.descriptor() >= 0) {
    loop.add_readable(source.descriptor(), pump);
  }

  loop.set_tick([&]() -> LoopControl {
    pump();
    static_cast<void>(ingestion.update());
    const auto telemetry = ingestion.view(kStallTimeoutMs);
    have_frame = telemetry.frame != nullptr;

    const auto now = Clock::now();
    if (now >= deadline) {
      return LoopControl::Stop;
    }

    if (now >= next_log) {
      next_log += kLogInterval;
      if (have_frame) {
        log_summary(*telemetry.frame, telemetry);
      } else {
        logging::warn(kUnit, "no frame yet; link=", telemetry::to_string(source.health().state));
      }
    }

    return LoopControl::Continue;
  });

  {
    logging::ScopedSink log_sink(io.log_sink());
    loop.run(stop);
  }

  io.finish();
  if (io.dropped_frames() != 0 || io.dropped_logs() != 0) {
    logging::warn(kUnit, "I/O queue overflow: dropped frames=", io.dropped_frames(), " logs=", io.dropped_logs());
  }

  const auto h = source.health();
  logging::info(kUnit, "stopped: frames ok=", h.frames_ok, " bad_crc=", h.frames_bad_crc,
                " malformed=", h.frames_malformed);

  if (h.frames_bad_crc + h.frames_malformed > 0) {
    logging::warn(kUnit, "rejected frames are counted, never rendered (spec §11)");
  }

  if (recorder != nullptr) {
    logging::info(kUnit, "recorded ", recorder->frames_written(), " frames to ", recorder->path());
    if (recorder->write_errors() > 0) {
      logging::error(kUnit, recorder->write_errors(), " frames could not be written to ", recorder->path());
    }
  }

  return have_frame ? 0 : 1;
}

}  // namespace c3::app
