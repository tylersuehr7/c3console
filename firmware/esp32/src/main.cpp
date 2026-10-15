// c3console sensor and relay board firmware — the MCU side of spec §3, §4, §6, §7.
//
// Every 100 ms: five conversions per channel on both ADS1115s in parallel at 128 SPS,
// median-of-5, per-channel IIR, lookup to engineering units, sender range check, fault
// evaluation with hysteresis, warning outputs, then one JSON frame with CRC on UART1.
// Conversions are polled rather than waited for, so commands from the Pi are parsed and
// acknowledged while the ADCs work. Relay outputs change only on a command and hold
// otherwise (spec §4.3).
//
// Safety invariants 1 and 2: the warning LED and buzzer are driven from here, from the same
// evaluation that fills faults[], and never wait for anything from the Pi.
//
// Deviation from spec §3.3's first draft ("8 SPS and oversample"): at 8 SPS one conversion
// takes 125 ms, so four channels with median-of-5 cannot fit a 10 Hz frame. 128 SPS (7.8 ms)
// with five samples per channel per tick does, and the IIR supplies the smoothing.

#include "ads1115.hpp"
#include "c3fw/calibration.hpp"
#include "c3fw/command.hpp"
#include "c3fw/faults.hpp"
#include "c3fw/filter.hpp"
#include "c3fw/frame_writer.hpp"
#include "c3fw/median.hpp"
#include "c3fw/relays.hpp"
#include "c3fw/sender_check.hpp"
#include "mailbox.hpp"
#include "pins.hpp"
#include "telemetry/frame.hpp"
#include "telemetry/protocol.hpp"

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace {

using c3::telemetry::Channel;
using c3::telemetry::Frame;
using c3::telemetry::kChannelCount;
using c3::vehicle::HvacBlower;

constexpr std::uint32_t kTickMs = 100;                 // 10 Hz, spec §4
constexpr std::uint32_t kConversionMs = 8;             // 128 SPS is 7.8 ms; the ready bit is polled after this
constexpr std::uint32_t kConversionTimeoutMs = 25;     // a chip that is not ready by now has a bus problem
constexpr std::size_t kSamplesPerTick = 5;             // median-of-5, spec §3.4
constexpr std::uint32_t kBaud = 115200;                // spec §4
constexpr std::size_t kRxBufferBytes = 1024;           // a full tick of line time; commands are < 64 bytes
constexpr std::uint32_t kRelayDwellMs = 30;            // blower tap release time + margin; CONFIRM on the relay board
constexpr std::uint32_t kStatusPeriodMs = 10000;       // counters on the USB log
constexpr std::uint32_t kSaveWorkerStackBytes = 8192;  // Preferences/NVS writes use most of 4 KB
constexpr const char* kNvsNamespace = "c3cal";

// Wrap-safe millis() comparison: true once `at` has been reached.
[[nodiscard]] bool reached(std::uint32_t now, std::uint32_t at) noexcept {
  return static_cast<std::int32_t>(now - at) >= 0;
}

HardwareSerial& pi_link = Serial1;
hw::Ads1115 adc_a(pins::kAdcFuelOil);
hw::Ads1115 adc_b(pins::kAdcCltVolt);

std::array<c3fw::Iir, kChannelCount> filters = {c3fw::Iir(c3fw::kFuelTauS), c3fw::Iir(c3fw::kOilTauS),
                                                c3fw::Iir(c3fw::kCoolantTauS), c3fw::Iir(c3fw::kVoltTauS)};
c3fw::Calibration calibration;
const std::array<c3fw::SenderWindow, kChannelCount> sender_windows = c3fw::default_sender_windows();
c3fw::FaultEvaluator faults;
c3fw::CommandedState commanded;
c3::telemetry::LineReader command_lines;

// Counters for the status line; nothing else reads them.
std::uint32_t frames_sent = 0;
std::uint32_t adc_errors = 0;
std::uint32_t tick_overruns = 0;
std::uint32_t tx_dropped = 0;

// ---------------------------------------------------------------------------
// UART1 transmit queue: frames and acks leave in order, in chunks the FIFO can take without
// blocking, so a slow line never stalls sampling.
// ---------------------------------------------------------------------------

struct TxLine {
  std::array<char, c3::telemetry::kMaxLineLength> bytes{};
  std::size_t size = 0;
};
hw::Mailbox<TxLine, 8> tx_lines;
std::optional<TxLine> transmitting;
std::size_t tx_position = 0;

void queue_line(const TxLine& line) {
  if (line.size != 0 && !tx_lines.push(line)) {
    ++tx_dropped;
  }
}

void service_tx() {
  if (!transmitting) {
    transmitting = tx_lines.pop();
    tx_position = 0;
  }
  if (!transmitting) {
    return;
  }
  const int available = pi_link.availableForWrite();
  if (available <= 0) {
    return;
  }
  const auto count = std::min({transmitting->size - tx_position, static_cast<std::size_t>(available), std::size_t{64}});
  tx_position += pi_link.write(reinterpret_cast<const std::uint8_t*>(transmitting->bytes.data() + tx_position), count);
  if (tx_position == transmitting->size) {
    transmitting.reset();
  }
}

// ---------------------------------------------------------------------------
// Relay outputs
// ---------------------------------------------------------------------------

constexpr std::array<int, c3fw::kOutputCount> kOutputPins = {
    pins::kBlower1,       pins::kBlower2,         pins::kBlower3,       pins::kBlower4,       pins::kAcClutch,
    pins::kFloorSolenoid, pins::kDefrostSolenoid, pins::kHeadlampRelay, pins::kHighBeamRelay, pins::kDoorRelay};

c3fw::Outputs applied{};  // what the pins show now

// Drives the pins to match `commanded`. Break before make on the blower taps: a tap that is
// going off is released first and the relay given time to actually drop out before the new
// tap is energised, so two resistor-pack taps are never bridged. The pause happens once per
// command; the sampler tolerates it.
void apply_outputs() {
  const c3fw::Outputs out = c3fw::outputs_for(commanded);
  bool released = false;
  for (std::size_t i = 0; i < c3fw::kBlowerTapCount; ++i) {
    if (applied[i] && !out[i]) {
      digitalWrite(kOutputPins[i], LOW);
      applied[i] = false;
      released = true;
    }
  }
  const bool engaging = std::any_of(out.begin(), out.begin() + c3fw::kBlowerTapCount, [](bool on) { return on; });
  if (released && engaging) {
    delay(kRelayDwellMs);
  }
  for (std::size_t i = 0; i < c3fw::kOutputCount; ++i) {
    digitalWrite(kOutputPins[i], out[i] ? HIGH : LOW);
  }
  applied = out;
}

// ---------------------------------------------------------------------------
// Calibration persistence (spec §4.2 cal add / cal save)
// ---------------------------------------------------------------------------

struct SaveRequest {
  Channel channel{};
  c3fw::Curve curve;
};
struct SaveResult {
  SaveRequest request;
  bool ok = false;
};
hw::Mailbox<SaveRequest, 1> save_requests;
hw::Mailbox<SaveResult, 1> save_results;
bool save_pending = false;
bool save_worker_ready = false;

const char* nvs_key(Channel ch) {
  switch (ch) {
    case Channel::Fuel:
      return "fuel";
    case Channel::Oil:
      return "oil";
    case Channel::Coolant:
      return "clt";
    case Channel::Volt:
      return "volt";
    case Channel::Count:
      break;
  }
  return "?";
}

// Activates every channel whose stored table is well formed; anything else keeps the
// placeholder and is reported as such at boot.
void load_curves() {
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, /*readOnly=*/true)) {
    return;
  }
  for (std::size_t i = 0; i < kChannelCount; ++i) {
    const char* key = nvs_key(static_cast<Channel>(i));
    std::array<c3fw::CalPoint, c3fw::kMaxCalPoints> pts{};
    const std::size_t bytes = prefs.getBytesLength(key);
    if (bytes < c3fw::Calibration::kMinimumPoints * sizeof(c3fw::CalPoint) || bytes > sizeof(pts) ||
        bytes % sizeof(c3fw::CalPoint) != 0) {
      continue;
    }
    if (prefs.getBytes(key, pts.data(), bytes) != bytes) {
      continue;
    }
    c3fw::Curve::Points points;
    for (std::size_t p = 0; p < bytes / sizeof(c3fw::CalPoint); ++p) {
      static_cast<void>(points.push_back(pts[p]));
    }
    if (const auto curve = c3fw::Curve::from_points(points)) {
      static_cast<void>(calibration.activate(static_cast<Channel>(i), *curve));
    }
  }
  prefs.end();
}

bool save_curve(const SaveRequest& request) {
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, /*readOnly=*/false)) {
    return false;
  }
  const c3fw::Curve& c = request.curve;
  std::array<c3fw::CalPoint, c3fw::kMaxCalPoints> pts{};
  for (std::size_t i = 0; i < c.size(); ++i) {
    pts[i] = c.points()[i];
  }
  const std::size_t bytes = c.size() * sizeof(c3fw::CalPoint);
  const bool ok = prefs.putBytes(nvs_key(request.channel), pts.data(), bytes) == bytes;
  prefs.end();
  return ok;
}

// NVS persistence never runs on the sampling task. Acknowledgement and activation wait
// for the result; a failed save leaves the active curve untouched.
void save_worker(void*) {
  for (;;) {
    if (const auto request = save_requests.pop()) {
      const SaveResult result{.request = *request, .ok = save_curve(*request)};
      static_cast<void>(save_results.push(result));  // only one save can be outstanding
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

void send_ack(std::string_view name, bool ok, std::string_view err) {
  TxLine line;
  line.size = c3fw::write_ack(name, ok, err, line.bytes);
  queue_line(line);
}

void handle_command(std::string_view text) {
  const c3fw::CommandResult r = c3fw::parse_command(text);
  if (!r.command) {
    send_ack(r.name, false, r.error);
    return;
  }
  const c3fw::Command& c = *r.command;
  switch (c.kind) {
    case c3fw::CommandKind::Hvac:
      commanded.relays.blower = static_cast<HvacBlower>(c.blower);  // the parser bounded it to 0..4
      commanded.relays.ac_enabled = c.ac;
      commanded.relays.mode = c.mode;
      apply_outputs();
      break;
    case c3fw::CommandKind::Lamp:
      commanded.relays.headlights_on = c.headlights;
      if (c.doors) {
        commanded.doors = *c.doors;
      }
      if (c.high) {
        commanded.high = *c.high;
      }
      apply_outputs();
      break;
    case c3fw::CommandKind::CalAdd:
      if (save_pending || !calibration.add(c.channel, {c.raw, c.value})) {
        send_ack(c.name, false, save_pending ? "save pending" : "invalid or full table");
        return;
      }
      break;
    case c3fw::CommandKind::CalSave: {
      const auto* curve = calibration.staged(c.channel);
      if (save_pending || !save_worker_ready || curve == nullptr) {
        send_ack(c.name, false, "save requires worker, idle state and 8 measured points");
        return;
      }
      save_pending = save_requests.push({.channel = c.channel, .curve = *curve});
      if (!save_pending) {
        send_ack(c.name, false, "save queue full");
      }
      return;  // acknowledged from loop() when the worker reports
    }
    case c3fw::CommandKind::Ping:
      break;
  }
  send_ack(c.name, true, {});
}

// ---------------------------------------------------------------------------
// Sampling
// ---------------------------------------------------------------------------

struct Conversion {
  hw::Ads1115* adc;
  hw::Mux mux;
  Channel channel;
};

// Both chips convert in parallel: pair k reads channel k of chip A and channel k of chip B.
constexpr std::array<std::array<Conversion, 2>, 2> kPairs = {{
    {{{&adc_a, hw::Mux::Diff01, Channel::Fuel}, {&adc_b, hw::Mux::Diff01, Channel::Coolant}}},
    {{{&adc_a, hw::Mux::Diff23, Channel::Oil}, {&adc_b, hw::Mux::Single2, Channel::Volt}}},
}};

// Collects five samples of every channel without blocking: one pair of conversions is in
// flight at a time and loop() polls for it, so the UART is serviced while the ADCs work.
// A full set takes about 85 ms at 128 SPS. Any bus error fails the whole set.
class Sampler {
public:
  enum class Status : std::uint8_t { Idle, Busy, Done, Failed };

  void begin(std::uint32_t now_ms) noexcept {
    sample_ = 0;
    pair_ = 0;
    status_ = start_pair(now_ms) ? Status::Busy : Status::Failed;
  }

  [[nodiscard]] Status poll(std::uint32_t now_ms) noexcept {
    if (status_ != Status::Busy || !reached(now_ms, started_ms_ + kConversionMs)) {
      return status_;
    }
    for (const Conversion& c : kPairs[pair_]) {
      if (!c.adc->ready()) {
        // Not finished, or a bus error that reads as "not ready"; the timeout tells them apart.
        if (reached(now_ms, started_ms_ + kConversionTimeoutMs)) {
          status_ = Status::Failed;
        }
        return status_;
      }
    }
    for (const Conversion& c : kPairs[pair_]) {
      std::int16_t v = 0;
      if (!c.adc->read(v)) {
        status_ = Status::Failed;
        return status_;
      }
      samples_[static_cast<std::size_t>(c.channel)][sample_] = v;
    }
    if (++pair_ == kPairs.size()) {
      pair_ = 0;
      ++sample_;
    }
    if (sample_ == kSamplesPerTick) {
      status_ = Status::Done;
    } else if (!start_pair(now_ms)) {
      status_ = Status::Failed;
    }
    return status_;
  }

  [[nodiscard]] Status status() const noexcept { return status_; }
  void reset() noexcept { status_ = Status::Idle; }

  /// Median-of-5 per channel of the completed set.
  void medians(std::array<std::int32_t, kChannelCount>& raw) const noexcept {
    for (std::size_t ch = 0; ch < kChannelCount; ++ch) {
      raw[ch] = c3fw::median5(samples_[ch]);
    }
  }

private:
  [[nodiscard]] bool start_pair(std::uint32_t now_ms) noexcept {
    for (const Conversion& c : kPairs[pair_]) {
      if (!c.adc->start(c.mux, hw::Rate::Sps128)) {
        return false;
      }
    }
    started_ms_ = now_ms;
    return true;
  }

  Status status_ = Status::Idle;
  std::size_t sample_ = 0;
  std::size_t pair_ = 0;
  std::uint32_t started_ms_ = 0;
  std::array<std::array<std::int16_t, kSamplesPerTick>, kChannelCount> samples_{};
};

Sampler sampler;
std::uint32_t next_tick_ms = 0;
std::uint32_t tick_ms = 0;  // when the set being published was started; the frame's `t`

// Turns a completed (or failed) sample set into the warning outputs and one frame.
void publish(std::uint32_t t_ms, bool sampled) {
  Frame f;
  f.t_ms = t_ms;
  std::array<std::int32_t, kChannelCount> raw{};
  if (sampled) {
    sampler.medians(raw);
  } else {
    ++adc_errors;
    raw.fill(c3fw::kFullScaleCounts);  // a dead bus is reported as every sender open, never as a frozen value
  }

  c3fw::Reading r;
  std::array<float, kChannelCount> values{};
  for (std::size_t ch = 0; ch < kChannelCount; ++ch) {
    f.raw[ch] = raw[ch];
    r.sender[ch] = sampled ? c3fw::check_sender(raw[ch], sender_windows[ch]) : c3fw::SenderState::Open;
    // Filter in counts, convert after: the lookup is nonlinear for the coolant NTC. A failed
    // set leaves the filters alone, so a bus glitch does not take three fuel time constants
    // to wash out.
    const float filtered =
        sampled ? filters[ch].update(static_cast<float>(raw[ch]), kTickMs / 1000.0F) : filters[ch].value();
    values[ch] = calibration.active(static_cast<Channel>(ch)).interpolate(static_cast<std::int32_t>(filtered));
  }
  r.fuel_fraction = values[static_cast<std::size_t>(Channel::Fuel)];
  r.oil_psi = values[static_cast<std::size_t>(Channel::Oil)];
  r.coolant_f = values[static_cast<std::size_t>(Channel::Coolant)];
  r.voltage_v = values[static_cast<std::size_t>(Channel::Volt)];
  f.fuel_fraction = r.fuel_fraction;
  f.oil_psi = r.oil_psi;
  f.coolant_f = r.coolant_f;
  f.voltage_v = r.voltage_v;
  f.faults = faults.evaluate(r);
  f.relays = commanded.relays;
  f.relays->lamp_doors_open = commanded.doors;
  f.relays->highbeams_on = commanded.high;
  f.reverse = digitalRead(pins::kReverseSense) == LOW;

  // Warning path: local, immediate, independent of the UART below (invariant 2).
  const c3fw::Warning w = faults.warning();
  digitalWrite(pins::kWarnLed, w.led ? HIGH : LOW);
  digitalWrite(pins::kBuzzer, w.buzzer ? HIGH : LOW);

  TxLine line;
  line.size = c3fw::write_frame(f, line.bytes);
  queue_line(line);
  ++frames_sent;
}

std::uint32_t next_status_ms = 0;

void print_status() {
  const c3fw::Warning w = faults.warning();
  Serial.printf("frames=%u adc_errors=%u overruns=%u tx_dropped=%u rx_dropped=%u led=%d buzzer=%d\n",
                static_cast<unsigned>(frames_sent), static_cast<unsigned>(adc_errors),
                static_cast<unsigned>(tick_overruns), static_cast<unsigned>(tx_dropped),
                static_cast<unsigned>(command_lines.dropped()), w.led ? 1 : 0, w.buzzer ? 1 : 0);
}

}  // namespace

void setup() {
  Serial.begin(kBaud);
  pi_link.setRxBufferSize(kRxBufferBytes);  // must precede begin()
  pi_link.begin(kBaud, SERIAL_8N1, pins::kPiRx, pins::kPiTx);
  Wire.begin(pins::kI2cSda, pins::kI2cScl);
  Wire.setClock(400000);

  pinMode(pins::kWarnLed, OUTPUT);
  pinMode(pins::kBuzzer, OUTPUT);
  pinMode(pins::kReverseSense, INPUT_PULLUP);
  for (const int pin : kOutputPins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  apply_outputs();  // everything off until the Pi commands otherwise

  load_curves();
  save_worker_ready = xTaskCreate(save_worker, "cal-save", kSaveWorkerStackBytes, nullptr, 1, nullptr) == pdPASS;
  Serial.printf("c3console mcu %s; adc 0x48 %s, 0x49 %s; calibration per channel:\n", C3FW_BUILD_STAMP,
                adc_a.probe() ? "ok" : "MISSING", adc_b.probe() ? "ok" : "MISSING");
  for (std::size_t i = 0; i < kChannelCount; ++i) {
    const auto channel = static_cast<Channel>(i);
    Serial.printf("  %s: %s\n", nvs_key(channel), calibration.measured(channel) ? "measured" : "PLACEHOLDER");
  }
  next_tick_ms = millis();
  next_status_ms = next_tick_ms + kStatusPeriodMs;
}

void loop() {
  const std::uint32_t now = millis();

  // Start a sample set on the tick. If the previous set ran past its tick the next one starts
  // as soon as it is done: one late frame, never a catch-up burst.
  if (sampler.status() == Sampler::Status::Idle && reached(now, next_tick_ms)) {
    if (reached(now, next_tick_ms + kTickMs)) {
      ++tick_overruns;
    }
    tick_ms = now;
    sampler.begin(now);
    next_tick_ms += kTickMs;
    if (reached(now, next_tick_ms)) {
      next_tick_ms = now + kTickMs;
    }
  }

  switch (sampler.poll(millis())) {
    case Sampler::Status::Done:
      publish(tick_ms, true);
      sampler.reset();
      break;
    case Sampler::Status::Failed:
      publish(tick_ms, false);
      sampler.reset();
      break;
    case Sampler::Status::Idle:
    case Sampler::Status::Busy:
      break;
  }

  if (const auto saved = save_results.pop()) {
    const bool activated = saved->ok && calibration.activate(saved->request.channel, saved->request.curve);
    send_ack("cal", activated, activated ? "" : "nvs write failed");
    save_pending = false;
  }

  // Commands arrive a line at a time; a bounded slice per loop keeps the sampler polled.
  for (unsigned bytes = 0; bytes < 64; ++bytes) {
    const int received_byte = pi_link.read();
    if (received_byte < 0) {
      break;
    }
    const char ch = static_cast<char>(received_byte);
    command_lines.feed(std::span<const char>(&ch, 1), handle_command);
  }

  service_tx();

  if (reached(now, next_status_ms)) {
    print_status();
    next_status_ms = now + kStatusPeriodMs;
  }
}
