// Global audio state.
//
// Audio is the Pi's job (spec §1): level and tone shaping run as a Pi-side DSP stage
// before the SPDIF output, and nothing here goes over UART. The ALSA stage does not
// exist yet, so the only sink today logs the state; the struct is the contract it
// consumes.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace c3::audio {

/// Supported pre-defined configurations for the sliders.
enum class Preset : std::uint8_t { Flat = 0, Rock, Cruise, Voice, Count };

[[nodiscard]] constexpr std::string_view to_string(Preset preset) noexcept {
  switch (preset) {
    case Preset::Flat:
      return "Flat";
    case Preset::Rock:
      return "Rock";
    case Preset::Cruise:
      return "Cruise";
    case Preset::Voice:
      return "Voice";
    case Preset::Count:
      break;
  }
  return "Flat";
}

struct AudioState {
  int volume = 62;  // 0 .. 100
  bool mute = false;
  int balance = 0;  // -10 (left) .. 10 (right)
  int fader = 0;    // -10 (rear) .. 10 (front)
  int sub_db = 0;   // -12 .. 6
  int bass_db = 0;  // -6 .. 6
  int mid_db = 0;
  int treble_db = 0;
  Preset preset = Preset::Flat;

  [[nodiscard]] constexpr bool operator==(const AudioState&) const noexcept = default;
};

/// Applies a preset's tone curve and records the preset.
[[nodiscard]] constexpr AudioState with_preset(AudioState state, Preset preset) noexcept {
  state.preset = preset;
  switch (preset) {
    case Preset::Flat:
      state.bass_db = 0;
      state.mid_db = 0;
      state.treble_db = 0;
      break;
    case Preset::Rock:
      state.bass_db = 4;
      state.mid_db = -1;
      state.treble_db = 3;
      break;
    case Preset::Cruise:
      state.bass_db = 2;
      state.mid_db = 0;
      state.treble_db = 1;
      break;
    case Preset::Voice:
      state.bass_db = -2;
      state.mid_db = 3;
      state.treble_db = 1;
      break;
    case Preset::Count:
      break;
  }
  return state;
}

/// Where audio state goes: the ALSA/DSP stage on the target, a logging sink until then.
class AudioSink {
public:
  virtual ~AudioSink() = default;
  AudioSink(const AudioSink&) = delete;
  AudioSink& operator=(const AudioSink&) = delete;
  AudioSink(AudioSink&&) = delete;
  AudioSink& operator=(AudioSink&&) = delete;

  virtual void apply(const AudioState& s) noexcept = 0;

protected:
  AudioSink() = default;
};

}  // namespace c3::audio
