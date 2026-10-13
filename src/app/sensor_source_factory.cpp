#include "sensor_source_factory.hpp"

#include "logging/log.hpp"
#include "telemetry/replay_source.hpp"

#include <memory>
#include <utility>

namespace c3::app {

SourceResult make_sensor_source(const Config& cfg, utils::MonotonicClock clock) {
  const auto stall_timeout_ms = static_cast<std::uint32_t>(cfg.stall_timeout_ms);  // validate() keeps it 1..1000
  switch (cfg.source) {
    case Source::Mock: {
      const auto profile = telemetry::parse_mock_profile(cfg.mock_profile);
      if (!profile) {  // validate() rejects this earlier; belt and braces
        return {.source = nullptr, .mock = nullptr, .error = "unknown mock profile '" + cfg.mock_profile + "'"};
      }
      auto mock = std::make_unique<telemetry::MockSource>(*profile, clock);
      telemetry::MockSource* const view = mock.get();
      return {.source = std::move(mock), .mock = view, .error = {}};
    }
    case Source::Replay: {
      telemetry::ReplaySource::Result replay = telemetry::ReplaySource::open(cfg.replay_file, clock, stall_timeout_ms);
      if (!replay.source) {
        return {.source = nullptr, .mock = nullptr, .error = "replay: " + replay.error};
      }
      for (const std::string& warning : replay.warnings) {
        logging::warn("replay", warning);
      }
      logging::info("replay", replay.summary);
      return {.source = std::move(replay.source), .mock = nullptr, .error = {}};
    }
    case Source::Uart: {
      telemetry::UartSource::Result uart = telemetry::UartSource::open(
          {.device = cfg.uart_device, .baud = cfg.baud, .stall_timeout_ms = stall_timeout_ms}, clock);
      if (!uart.source) {
        return {.source = nullptr, .mock = nullptr, .error = "uart: " + uart.error};
      }
      auto* const commands = uart.source.get();  // the UART carries commands as well as frames
      return {.source = std::move(uart.source), .mock = nullptr, .error = {}, .commands = commands};
    }
  }
  return {.source = nullptr, .mock = nullptr, .error = "unknown source"};
}

}  // namespace c3::app
