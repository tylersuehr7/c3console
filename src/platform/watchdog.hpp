// Watchdog — the hook the event loop kicks after every completed render tick (spec §5.6).
//
// Target: /dev/watchdog via bcm2835_wdt.
// Hung render process reboots the box within 15s (acceptance §11).
#pragma once

namespace c3::platform {

class Watchdog {
public:
  virtual ~Watchdog() = default;

  Watchdog(const Watchdog&) = delete;
  Watchdog& operator=(const Watchdog&) = delete;
  Watchdog(Watchdog&&) = delete;
  Watchdog& operator=(Watchdog&&) = delete;

  /// Called once per completed tick from the event loop.
  /// *Must not block or allocate.*
  virtual void kick() noexcept = 0;

protected:
  Watchdog() = default;
};

/// For the DESKTOP backend. The loop kicks only after a tick has finished,
/// so a stuck draw or a blocked swap stops the kicks; that is the point.
class NullWatchdog final : public Watchdog {
public:
  void kick() noexcept override {}
};

};  // namespace c3::platform
