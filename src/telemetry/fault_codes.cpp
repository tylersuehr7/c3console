#include "fault_codes.hpp"

#include <algorithm>
#include <array>

namespace c3::telemetry {

std::optional<Channel> parse_channel(std::string_view text) noexcept {
  std::uint8_t i;
  for (i = 0; i < static_cast<std::uint8_t>(Channel::Count); i++) {
    const auto channel = static_cast<Channel>(i);
    if (text == to_string(channel)) {
      return channel;
    }
  }
  return std::nullopt;
}

std::optional<Fault> parse_fault_code(std::string_view code) noexcept {
  std::uint8_t i;
  for (i = 0; i < static_cast<std::uint8_t>(FaultKind::Count); i++) {
    const auto kind = static_cast<FaultKind>(i);
    const std::string_view base = to_string(kind);

    if (!has_channel(kind)) {
      if (code == base) {
        return Fault{.kind = kind, .channel = Channel{}};
      }
      continue;
    }

    // "<base>_<channel>"
    if (code.size() > base.size() + 1 && code.starts_with(base) && code[base.size()] == '_') {
      if (const auto channel = parse_channel(code.substr(base.size() + 1))) {
        return Fault{.kind = kind, .channel = *channel};
      }
    }
  }

  return std::nullopt;
}

std::size_t format_fault_code(Fault fault, std::span<char> out) noexcept {
  const std::string_view base = to_string(fault.kind);

  std::size_t needed = base.size();
  std::string_view suffix;

  if (has_channel(fault.kind)) {
    suffix = to_string(fault.channel);
    needed += 1 + suffix.size();
  }

  if (out.size() < needed) {
    return 0;
  }

  auto it = std::copy(base.begin(), base.end(), out.begin());
  if (has_channel(fault.kind)) {
    *it++ = '_';
    std::copy(suffix.begin(), suffix.end(), it);
  }

  return needed;
}

}  // namespace c3::telemetry