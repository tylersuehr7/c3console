#pragma once

#include "utils/fixed_text.hpp"

#include <cstdint>
#include <string_view>

namespace c3::logging {

enum class Level : uint8_t { Debug = 0, Info, Warn, Error };

constexpr std::string_view to_string(Level level) noexcept {
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
constexpr std::string_view to_priority_prefix(Level level) noexcept {
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
constexpr const char* to_ascii_color(Level level) noexcept {
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

constexpr void debug(std::string_view tag, std::string_view message) noexcept {
  write(Level::Debug, tag, message);
}

constexpr void info(std::string_view tag, std::string_view message) noexcept {
  write(Level::Info, tag, message);
}

constexpr void warn(std::string_view tag, std::string_view message) noexcept {
  write(Level::Warn, tag, message);
}

constexpr void error(std::string_view tag, std::string_view message) noexcept {
  write(Level::Error, tag, message);
}

namespace {
template <typename... Args>
constexpr void emit(Level level, std::string_view tag, Args&&... args) noexcept {
  if (get_level() <= level) {
    utils::FixedText<768> text;
    (text.append(std::forward<Args>(args)), ...);
    write(level, tag, text.view());
  }
}
}  // namespace

template <typename... Args>
constexpr void debug_va(std::string_view tag, Args&&... args) noexcept {
  emit(Level::Debug, tag, std::forward<Args>(args)...);
}

template <typename... Args>
constexpr void info_va(std::string_view tag, Args&&... args) noexcept {
  emit(Level::Info, tag, std::forward<Args>(args)...);
}

template <typename... Args>
constexpr void warn_va(std::string_view tag, Args&&... args) noexcept {
  emit(Level::Warn, tag, std::forward<Args>(args)...);
}

template <typename... Args>
constexpr void error_va(std::string_view tag, Args&&... args) noexcept {
  emit(Level::Error, tag, std::forward<Args>(args)...);
}

}  // namespace c3::logging
