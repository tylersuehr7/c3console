#include "file_watchdog.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <system_error>

namespace c3::platform {

FileWatchdog::Result FileWatchdog::open(const std::string& path) {
  utils::UniqueFd fd(::open(path.c_str(), O_WRONLY | O_CLOEXEC));

  if (!fd) {
    return {
        .watchdog = nullptr,
        .error = "watchdog: cannot open " + path + ": " + std::system_category().message(errno),
    };
  }

  return {
      .watchdog = std::unique_ptr<FileWatchdog>(new FileWatchdog(std::move(fd))),
      .error = {},
  };
}

FileWatchdog::~FileWatchdog() {
  if (fd_ && clean_exit_) {
    static_cast<void>(::write(fd_.get(), "V", 1));  // magic close: this exit is deliberate
  }
}

void FileWatchdog::kick() noexcept {
  if (::write(fd_.get(), "k", 1) == 1) {
    ++kicks_;
  } else {
    ++failures_;
  }
}

}  // namespace c3::platform
