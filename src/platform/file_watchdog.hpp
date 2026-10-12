// FileWatchdog — kicks a Linux watchdog device (spec §5.6: bcm2835_wdt on /dev/watchdog).
//
// Any write keeps the timer alive; the magic character 'V' on close tells the driver the
// close was deliberate so a clean exit does not reboot the box. A crash or hang skips the
// magic close and the hardware reboots within the driver's timeout, which is the point
// (acceptance §11: killing the render process reboots within 15s). The path is a config
// value so tests can point it at an ordinary file.
#pragma once

#include "utils/unique_fd.hpp"
#include "watchdog.hpp"

#include <memory>
#include <string>

namespace c3::platform {

class FileWatchdog final : public Watchdog {
public:
  struct Result {
    std::unique_ptr<FileWatchdog> watchdog;  // null when `error` is set
    std::string error;
  };

  ~FileWatchdog() override;
  FileWatchdog(const FileWatchdog&) = delete;
  FileWatchdog& operator=(const FileWatchdog&) = delete;
  FileWatchdog(FileWatchdog&&) = delete;
  FileWatchdog& operator=(FileWatchdog&&) = delete;

  void kick() noexcept override;
  void allow_clean_exit() noexcept { clean_exit_ = true; }

  [[nodiscard]] unsigned kicks() const noexcept { return kicks_; }
  [[nodiscard]] unsigned failures() const noexcept { return failures_; }

  /// Opens the device for writing. Init-time.
  [[nodiscard]] static Result open(const std::string& path);

private:
  explicit FileWatchdog(utils::UniqueFd fd) noexcept : fd_(std::move(fd)) {}

  utils::UniqueFd fd_;
  bool clean_exit_ = false;
  unsigned kicks_ = 0;
  unsigned failures_ = 0;
};

}  // namespace c3::platform
