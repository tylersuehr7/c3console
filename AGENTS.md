Custom instrument and infotainment console for a 1975 Chevrolet Corvette Stingray (C3, 350 small-block). C++20, Raspberry Pi CM4, portrait touchscreen. Renders auxiliary gauges around an embedded Apple CarPlay video surface, drives audio over SPDIF, and controls HVAC relays through a companion microcontroller.

## Read these first

- `mockups/c3console-ui-mockup.html` — a working browser prototype of the exact UI. Its stepModel() function is the reference implementation for MockSource, and its Canvas 2D gauge drawing is the reference for the NanoVG renderer. Port from it; don't reinvent it.

## Safety invariants — never violate these

These are not style preferences. They are the reason this system is allowed in a car at all.

1. **MCU owns everything safety-adjacent.** Sensor acquisition, filtering, unit conversion, relay drive, and the low-oil-pressure / high-coolant-temp warning buzzer all live in firmware/esp32. The Pi is display, CarPlay, and audio only. Never move a warning decision onto the Pi.
2. **Warning path is local to the MCU and independent of UART.** It must fire with the Pi powered off. Never gate it on a command from the host.
3. **Never place the Pi or the MCU in series with the headlamp circuit.** Lamp control is a relay wired in parallel with the factory switch, which stays authoritative. Code that assumes exclusive control of the lamps is wrong.
4. **Never display a stale sensor value as though it were live.** If telemetry stalls for more than 1s, gauges grey out and show a comms fault. A frozen needle is worse than no needle.
5. **Never fabricate sender calibration constants.** The real curves come from bench measurement. Ship an explicitly-marked placeholder table in config/calibration.json and read from it. Do not hardcode a curve fit and do not present invented numbers as calibrated.
6. **HVAC holds last state on comms loss.** Do not fail the blower to off. Defrost must remain reachable without software.

## Conventions

- C++20. `std::optional`, `std::span`, designated initializers, `constexpr`.
- **No heap allocation in the render loop or the telemetry hot path.** Preallocate at init. Fixed-size buffers with explicit capacity.
- **No exceptions in the render loop.** Errors are return values there. Exceptions are acceptable during startup and config load.
- **No locks in the render path.** Sensor state crosses threads via a snapshot buffer with an atomic index (util/triple_buffer.h; three slots, because two cannot rule out a torn read).
- RAII wrappers for every C handle: `libusb_device_handle`, `drmModeAtomicReq`, file descriptors, EGL objects. No bare `libusb_close` calls in the codebase.
- `snake_case` for functions and variables, `PascalCase` for types, `kConstantCase` for constants. File names are `snake_case` (`display_backend.h`, `console_scene.cpp`).
- Error handling: `std::expected-style` result types, or `std::optional` plus a logged reason. No silent failures and no `assert` standing in for validation of external input.
- Format with `clang-format` (Google base, 2-space indent, 120 columns). Run `clang-tidy` before finishing a milestone.

## Dependencies

Keep the dependency list short and justify additions.

- **NanoVG** — vendored as a submodule, the only rendering abstraction
- **SDL2** — desktop backend only, never linked into the target build
- **libdrm**, **libgbm**, **EGL**, **GLESv2** — target backend
- **libusb-1.0** — CarPlay dongle transport
- **libinput**, **libudev** — target touch input
- **nlohmann/json** — config and calibration files, init-time only
- **Catch2** — tests

Telemetry line parsing does *not* use nlohmann. Write a hand-rolled fixed-buffer parser in `telemetry/protocol.cpp`: no allocation, no exceptions, returns `std::optional<Frame>`.

## Testing expectations

Pure functions get unit tests, no exceptions. In particular:

- CRC-16/CCITT against known vectors
- Protocol parsing: valid, truncated, malformed, bad CRC, missing fields, out-of-range values, garbage bytes mid-stream
- Calibration table interpolation, including extrapolation beyond the last point
- Fault threshold evaluation and hysteresis
- Layout arithmetic, so gauge and well rects stay correct at any panel size
- Touch coordinate transform, including points exactly on the well boundary

Integration behavior that needs a rig is verified against the acceptance checklist in spec §11, not with mocks pretending to be hardware.

## Things not to do

- Do not attempt DRM atomic multi-plane compositing until EGLImage compositing (stage 1) runs in the car. The spec is explicit about this ordering.
- Do not implement the CarPlay dongle protocol from imagination. Reference `FastCarPlay` (C++, SDL2 + FFmpeg + libusb) and `node-carplay`. If protocol details are uncertain, say so and stop rather than guessing message framing.
- Do not hardcode the dongle USB product ID. Both `0x1520` and `0x1521` exist in the wild; make it configurable and enumerate.
- Do not add a UI framework. NanoVG and hand-drawn scenes only.
- Do not write to the root filesystem at runtime. The target runs read-only with overlayfs; logs go to tmpfs.
- Do not use `std::cout` for logging. Use the project logger with levels and a systemd-journal-friendly format.

## Hardware facts the model cannot infer

- Senders are single-wire resistive, grounded through the block or tank, read by piggyback tap. Factory gauges stay connected and must keep working.
- Nominal ranges: fuel 0 Ω empty → 90 Ω full; oil pressure ~1 Ω @ 0 psi → ~90 Ω @ 80 psi; coolant NTC ~1365 Ω @ 100 °F → ~55 Ω @ 250 °F. All are starting hypotheses, not calibration data.
- Four differential ADC channels require two ADS1115 chips (0x48 and 0x49). One chip gives four single-ended or only two differential pairs.
- CM4 has a hardware H.264 decode block; Pi 5 does not. The decode path targets V4L2 M2M with DMA-BUF output, imported via `EGL_EXT_image_dma_buf_import`.
- Filter time constants differ per channel by design: fuel τ≈10 s, coolant τ≈2 s, oil pressure τ≈200 ms. Do not unify them — a slow oil pressure decay that gets filtered into invisibility is the failure that destroys the engine.

## Working style

- Small commits, one logical change each. One milestone per branch.
- Run the tests and report actual output. Never claim a build or test passes without having run it.
- Run `/cpp-review` before finishing a milestone; it enforces the checklist in `.claude/skills/cpp-review/SKILL.md` and re-verifies on both toolchains.
- When a decision has real tradeoffs — a dependency, a threading change, a protocol change — stop and ask rather than picking silently.
- This is a hobby project on a valuable car. Correctness and reversibility beat cleverness and feature count every time.
