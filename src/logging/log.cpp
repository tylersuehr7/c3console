#include "log.hpp"

#include <atomic>
#include <cstdio>

#include "utils/fixed_text.hpp"

namespace c3::logging {

namespace {
std::atomic<Level>& level_storage() noexcept {
  static std::atomic<Level> level{Level::Debug};
  return level;
}

Sink& sink_storage() noexcept {
  static thread_local Sink sink;
  return sink;
}
}  // namespace

void set_level(Level level) noexcept { level_storage().store(level, std::memory_order_relaxed); }

[[nodiscard]] Level get_level() noexcept { return level_storage().load(std::memory_order_relaxed); }

[[nodiscard]] Sink set_sink(Sink sink) noexcept {
  const auto previous = sink_storage();
  sink_storage() = sink;
  return previous;
}

bool write(Level level, std::string_view tag, std::string_view message) noexcept {
  if (get_level() > level) {
    return false;
  }

  const char *ascii_color = to_ascii_color(level);

  auto text = utils::FixedText<1024>();
  text.append(to_priority_prefix(level));
  text.append("\033[0;");
  text.append(ascii_color);
  text.append('m');
  text.append('[');
  text.append(to_string(level));
  text.append("] ");
  text.append('(');
  text.append(tag);
  text.append(") ");
  text.append(message);
  text.append("\033[0m");
  text.append('\n');

  const auto line = text.view();

  if (sink_storage().emit != nullptr) {
    sink_storage().emit(sink_storage().context, line);
  } else {
    // Note: never use `cout` for application logging
    static_cast<void>(std::fwrite(line.data(), 1, line.size(), stderr));
  }

  return true;
}

}  // namespace c3::logging
