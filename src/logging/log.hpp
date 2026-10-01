#pragma once

#include <string_view>

namespace c3::logging {

enum class Level { Debug = 0, Info, Warn, Error };

inline constexpr std::string_view to_string(Level level) noexcept {
  switch (level) {
    case Level::Debug:
      return "debug";
    case Level::Info:
      return "info";
    case Level::Warn:
      return "warn";
    case Level::Error:
      return "error";
  }
  return "?";
}

// sd-daemon priority prefix, parsed by journald when SyslogLevelPrefix=yes (the default).
inline constexpr std::string_view to_priority_prefix(Level level) noexcept {
  switch (level) {
    case Level::Debug:
      return "<7>";
    case Level::Info:
      return "<6>";
    case Level::Warn:
      return "<4>";
    case Level::Error:
      return "<3>";
  }
  return "<6>";
}

// ASCII terminal colors.
inline constexpr const char* to_ascii_color(Level level) noexcept {
  switch (level) {
    case Level::Debug:
      return "39";
    case Level::Info:
      return "34";
    case Level::Warn:
      return "33";
    case Level::Error:
      return "31";
  }
  return "39";
}

void set_level(Level level) noexcept;

[[nodiscard]] Level get_level() noexcept;

/// Callback mechanism for intercepting logging writes
struct Sink {
  void* context = nullptr;
  void (*emit)(void*, std::string_view) noexcept = nullptr;
};

/// @brief Sets a given sink to intercept logging writes.
/// @param sink the sink
/// @return the previous sink
[[nodiscard]] Sink set_sink(Sink sink) noexcept;

class ScopedSink {
 public:
  explicit ScopedSink(Sink sink) noexcept : previous_(set_sink(sink)) {}
  ~ScopedSink() { static_cast<void>(set_sink(previous_)); }
  ScopedSink(const ScopedSink&) = delete;
  ScopedSink& operator=(const ScopedSink&) = delete;
  ScopedSink(ScopedSink&&) = delete;
  ScopedSink& operator=(ScopedSink&&) = delete;

 private:
  Sink previous_;
};

bool write(Level level, std::string_view tag, std::string_view message) noexcept;

inline constexpr void debug(std::string_view tag, std::string_view message) noexcept {
  write(Level::Debug, tag, message);
}

inline constexpr void info(std::string_view tag, std::string_view message) noexcept {
  write(Level::Info, tag, message);
}

inline constexpr void warn(std::string_view tag, std::string_view message) noexcept {
  write(Level::Warn, tag, message);
}

inline constexpr void error(std::string_view tag, std::string_view message) noexcept {
  write(Level::Error, tag, message);
}

}  // namespace c3::logging