# firmware/esp32

PlatformIO project for the ESP32-S3 sensor and relay board (spec §2, §3, §4, §6, §7). It owns
sensor acquisition, filtering, unit conversion, fault evaluation, relay drive and the
independent warning path (AGENTS.md safety invariants 1 and 2).

```
lib/c3fw/         pure logic, no Arduino: median, IIR, lookup, sender windows, fault evaluator
                  with hysteresis, relay outputs, frame writer (CRC), command parser.
                  Built on the host as `c3_firmware` and exercised by tests/test_firmware_*.cpp
                  against the Pi's own parser and formatters.
src/main.cpp      Arduino hardware layer: 10 Hz tick, non-blocking ADS1115 sampler, UART1 to
                  the Pi, GPIO outputs, NVS calibration storage.
src/ads1115.*     minimal single-shot ADS1115 driver over Wire.
include/pins.hpp  BENCH pin assignments. Confirm against the relay board, then freeze.
```

Build and flash:

```bash
pio run                 # needs PlatformIO; the platform and GCC 13 toolchain download on first run
pio run -t upload
pio device monitor      # human log on USB (UART0); telemetry is on UART1, pins 17/18
```

The host tests are part of the normal suite:

```bash
cmake --build build --target c3console_tests && ./build/tests/c3console_tests "[firmware]"
python3 tools/generate_calibration.py --check   # lookup.hpp still matches config/calibration.json
```

Bench check against the desktop app (Tier 2): wire UART1 to a USB serial adapter and run
`./build/c3console --source=uart --headless=10` with `uart_device` pointing at it, then the UI
with `--desktop --source=uart`. Expected: `link=live`, four readable channels, `SENDER_OPEN_*`
when a pot is unplugged, CRC failures counted and dropped when the line is disturbed. With the
placeholder curves a coolant pot turned high reads hot enough for `CLT_HIGH`; that is the
placeholder, not a fault.

The USB log prints a counter line every 10 s: frames sent, ADC bus errors, tick overruns,
dropped transmit lines, dropped receive lines, and the LED and buzzer state.

## Decisions

- **Platform.** pioarduino's `platform-espressif32` (Arduino core 3.x, ESP-IDF 5, GCC 13). The
  registry's `platformio/espressif32` still ships GCC 8.4, which lacks `<span>` and the rest of
  the C++20 library the shared Pi headers need.
- **CRC.** CRC-16/CCITT-FALSE over the object without the crc member, via the Pi's
  `telemetry/crc16.hpp`. The frame writer and the Pi parser are round-tripped in
  `tests/test_firmware_codec.cpp`.
- **Sampling.** 128 SPS with median-of-5 per channel per 100 ms tick, not the spec's first-draft
  8 SPS: four channels cannot fit a 10 Hz frame at 125 ms per conversion. Conversions are
  polled from `loop()` rather than waited for, so the UART is serviced while the ADCs work. A
  set that runs past its tick delays the next frame by that much; it never triggers a burst.
- **Filter in raw counts, convert after**: the coolant lookup is nonlinear. A failed sample set
  leaves the filters alone, so a bus glitch does not take three fuel time constants to wash out.
- **Sender windows.** Open detected on fuel, oil, coolant (> 97 % of full scale); short only on
  coolant (fuel and oil legitimately read near 0 Ω). Volt has no window; `VOLT_LOW` covers it.
- **Warning path fails toward warning.** The buzzer for `OIL_LOW` is gated on the alternator
  charging (volts ≥ 13.2) so 0 psi at key-on-engine-off is silent; if the voltage channel
  itself cannot be read the gate is treated as open and the buzzer sounds. The LED also lights
  when the oil or coolant sender cannot be read, so a dead I2C bus is never a dark panel.
  `CLT_HIGH` always sounds.
- **Relays.** Blower taps switch break-before-make with a dwell (`kRelayDwellMs`, to be
  confirmed against the relay board's release time) between releasing the old tap and
  energising the new one. A/C without the blower is refused at the command parser with an ack
  error, and the output map drops the clutch regardless. Outputs change only on a command and
  hold otherwise (spec §4.3).
- **Calibration.** `cal add` updates a separate RAM staging table. `cal save` requires at least
  eight points and sends the curve to the NVS worker (`Preferences`, namespace `c3cal`). Only a
  successful save activates that channel's curve and marks it measured; failure retains the
  previous active curve and the staged points. There is no `cal clear`: a wrong point can be
  replaced by adding again at the same raw value, or the staging table discarded by rebooting.
  Flash/cache timing during a save still needs bench measurement.
- **Defaults.** `lib/c3fw/src/c3fw/lookup.hpp` is generated from `config/calibration.json` by
  `tools/generate_calibration.py`; a host test fails if they drift. The curves are placeholders
  by declaration (`kPlaceholderCalibration`), never bench data.
- A dead I2C bus reports every sender open rather than freezing a value.
