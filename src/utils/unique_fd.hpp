// RAII owner of a POSIX file descriptor (the spec calls for RAII around every C handle).
//
// Closes on destruction, moves transfer ownership, and -1 means "no descriptor". The get,
// release and reset trio follows std::unique_ptr so the shape is familiar.
#pragma once

#include <unistd.h>

#include <utility>

namespace c3::utils {

/// Move-only owner of one file descriptor; closes it when destroyed.
class UniqueFd {
public:
  /// Creates an empty handle that owns nothing.
  UniqueFd() noexcept = default;

  /// Takes ownership of a descriptor.
  ///
  /// @param fd the descriptor to own; a negative value means "nothing to close"
  explicit UniqueFd(int fd) noexcept : fd_(fd) {}

  /// Closes the descriptor if one is held.
  ///
  /// The result of close() is ignored: by the time a destructor runs there is nothing useful
  /// left to do about it.
  ~UniqueFd() { reset(); }

  // Copying would close the same descriptor twice.
  UniqueFd(const UniqueFd&) = delete;
  UniqueFd& operator=(const UniqueFd&) = delete;

  /// Takes ownership from another handle, leaving it empty.
  ///
  /// @param other the handle to take from
  UniqueFd(UniqueFd&& other) noexcept : fd_(other.release()) {}

  /// Closes any held descriptor, then takes ownership from another handle, leaving it empty.
  ///
  /// Self-assignment is a no-op.
  ///
  /// @param other the handle to take from
  /// @return this handle
  UniqueFd& operator=(UniqueFd&& other) noexcept {
    if (this != &other) {
      reset(other.release());
    }
    return *this;
  }

  /// @return the raw descriptor for passing to C APIs, or -1; ownership stays here
  [[nodiscard]] int get() const noexcept { return fd_; }

  /// @return true if a descriptor is held; otherwise false
  [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }

  /// Same as valid(), for `if (fd)`.
  explicit operator bool() const noexcept { return valid(); }

  /// Gives up ownership without closing and leaves the handle empty.
  ///
  /// The caller becomes responsible for closing the returned descriptor.
  ///
  /// @return the descriptor that was held, or -1
  [[nodiscard]] int release() noexcept { return std::exchange(fd_, -1); }

  /// Closes the held descriptor, if any, then adopts a new one.
  ///
  /// Do not pass the descriptor already held: it would be closed and then kept.
  ///
  /// @param fd the descriptor to adopt; defaults to none
  void reset(int fd = -1) noexcept {
    if (fd_ >= 0) {
      static_cast<void>(::close(fd_));
    }
    fd_ = fd;
  }

private:
  int fd_ = -1;
};

}  // namespace c3::utils
