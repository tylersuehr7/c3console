#include "c3fw/frame_writer.hpp"

#include "telemetry/crc16.hpp"
#include "telemetry/fault_codes.hpp"
#include "vehicle/vehicle_commands.hpp"

#include <array>
#include <cstdio>

namespace c3fw {
namespace {

// Appends printf output at `len`; false (and len unchanged) when it would not fit.
template <typename... Args>
[[nodiscard]] bool append(std::span<char> out, std::size_t& len, const char* fmt, Args... args) noexcept {
  if (len >= out.size()) {
    return false;
  }

  const int n = std::snprintf(out.data() + len, out.size() - len, fmt, args...);  // NOLINT
  if (n < 0 || static_cast<std::size_t>(n) >= out.size() - len) {
    return false;
  }

  len += static_cast<std::size_t>(n);
  return true;
}

[[nodiscard]] constexpr bool is_token_char(char c) noexcept {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// The ack echoes the command name the Pi sent. Only a plain token goes back verbatim; anything
// that could break the ack's own JSON (quotes, backslashes, control bytes) becomes "?".
[[nodiscard]] std::string_view ack_name(std::string_view cmd) noexcept {
  if (cmd.empty()) {
    return "?";
  }
  for (const char c : cmd) {
    if (!is_token_char(c)) {
      return "?";
    }
  }
  return cmd;
}

}  // namespace

std::size_t write_frame(const c3::telemetry::Frame& f, std::span<char> out) noexcept {
  using c3::vehicle::RelayState;
  std::size_t len = 0;
  bool ok = append(out, len, R"({"t":%u,"fuel":%.4f,"oil":%.2f,"clt":%.2f,"volt":%.3f,"faults":[)",
                   static_cast<unsigned>(f.t_ms), static_cast<double>(f.fuel_fraction), static_cast<double>(f.oil_psi),
                   static_cast<double>(f.coolant_f), static_cast<double>(f.voltage_v));
  std::array<char, c3::telemetry::kMaxFaultCodeLength> code{};
  for (std::size_t i = 0; ok && i < f.faults.size(); ++i) {
    const std::size_t n = c3::telemetry::format_fault_code(f.faults[i], code);
    ok = append(out, len, i == 0 ? "\"%.*s\"" : ",\"%.*s\"", static_cast<int>(n), code.data());
  }
  ok = ok && append(out, len, R"(],"raw":{"fuel":%ld,"oil":%ld,"clt":%ld,"volt":%ld})", static_cast<long>(f.raw[0]),
                    static_cast<long>(f.raw[1]), static_cast<long>(f.raw[2]), static_cast<long>(f.raw[3]));
  const RelayState relays = f.relays.value_or(RelayState{});
  const std::string_view mode = c3::vehicle::to_string(relays.mode);
  ok = ok && append(out, len, R"(,"relays":{"blower":%u,"ac":%s,"mode":"%.*s","headlights":%s)",
                    static_cast<unsigned>(relays.blower), relays.ac_enabled ? "true" : "false",
                    static_cast<int>(mode.size()), mode.data(), relays.headlights_on ? "true" : "false");
  if (relays.lamp_doors_open) {
    ok = ok && append(out, len, R"(,"doors":%s)", *relays.lamp_doors_open ? "true" : "false");
  }
  if (relays.highbeams_on) {
    ok = ok && append(out, len, R"(,"high":%s)", *relays.highbeams_on ? "true" : "false");
  }
  ok = ok && append(out, len, "%s", "}");
  ok = ok && append(out, len, R"(,"rev":%s})", f.reverse ? "true" : "false");
  if (!ok) {
    return 0;
  }
  // CRC over the object as it stands (closing brace included), then splice the crc member in
  // where the brace was. telemetry/protocol.cpp check_crc() undoes exactly this.
  const std::uint16_t crc = c3::telemetry::Crc16Ccitt::compute(std::string_view(out.data(), len));
  --len;  // drop the '}'
  if (!append(out, len,
              R"(,"crc":"%04x"})"
              "\n",
              static_cast<unsigned>(crc))) {
    return 0;
  }
  return len;
}

std::size_t write_ack(std::string_view cmd, bool ok, std::string_view err, std::span<char> out) noexcept {
  const std::string_view name = ack_name(cmd);
  std::size_t len = 0;
  bool fits =
      append(out, len, R"({"ack":"%.*s","ok":%s)", static_cast<int>(name.size()), name.data(), ok ? "true" : "false");
  if (fits && !ok) {
    fits = append(out, len, R"(,"err":"%.*s")", static_cast<int>(err.size()), err.data());
  }
  fits = fits && append(out, len, "%s", "}\n");
  return fits ? len : 0;
}

}  // namespace c3fw
