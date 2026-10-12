// DebugFrameCapture — development hook that write the next presented frame to a file.
//
// Implemented by DesktopBackend so a run can be checked without a human looking at the
// window (--screenshot=PATH). Not part of the render loop's steady state; the target
// backend does not implement it.
#pragma once

#include <string_view>

namespace c3::platform {

class DebugFrameCapture {
public:
  virtual ~DebugFrameCapture() = default;

  DebugFrameCapture(const DebugFrameCapture&) = delete;
  DebugFrameCapture& operator=(const DebugFrameCapture&) = delete;
  DebugFrameCapture(DebugFrameCapture&&) = delete;
  DebugFrameCapture& operator=(DebugFrameCapture&&) = delete;

  /// Arms a capture of the frame currently being drawn; it is written during end_frame().
  /// Returns false if the path cannot be kept (too long) or a capture is already pending.
  [[nodiscard]] virtual bool request_capture(std::string_view ppm_path) noexcept = 0;

protected:
  DebugFrameCapture() = default;
};

}  // namespace c3::platform