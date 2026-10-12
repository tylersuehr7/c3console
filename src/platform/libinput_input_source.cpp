#include "libinput_input_source.hpp"

#include "logging/log.hpp"

#include <fcntl.h>
#include <libinput.h>
#include <libudev.h>
#include <unistd.h>

#include <cerrno>
#include <system_error>

namespace c3::platform {

namespace {
constexpr std::string_view kUnit = "touch";

int open_restricted(const char* path, int flags, void* /*user_data*/) {
  const int fd = ::open(path, flags | O_CLOEXEC);  // NOLINT(cppcoreguidelines-pro-type-vararg)
  return fd < 0 ? -errno : fd;
}

void close_restricted(int fd, void* /*user_data*/) {
  static_cast<void>(::close(fd));
}

constexpr libinput_interface kInterface = {
    .open_restricted = open_restricted,
    .close_restricted = close_restricted,
};
}  // namespace

LibinputInputSource::Result LibinputInputSource::create(const Options& options) {
  if (options.native.width <= 0 || options.native.height <= 0) {
    return {nullptr, "touch: native panel size must be positive"};
  }

  std::unique_ptr<LibinputInputSource> s(new LibinputInputSource(options.native, options.rotation));
  s->udev_ = udev_new();
  if (s->udev_ == nullptr) {
    return {nullptr, "touch: udev_new failed"};
  }

  s->li_ = libinput_udev_create_context(&kInterface, nullptr, s->udev_);
  if (s->li_ == nullptr) {
    return {nullptr, "touch: libinput_udev_create_context failed"};
  }

  if (libinput_udev_assign_seat(s->li_, options.seat.c_str()) != 0) {
    return {nullptr, "touch: cannot assign seat '" + options.seat + "' (is the service in the input group?)"};
  }

  logging::info(kUnit, "libinput on seat ", options.seat, ", native ", options.native.width, "x", options.native.height,
                " rotation ", to_degrees(options.rotation));

  return {std::move(s), {}};
}

LibinputInputSource::~LibinputInputSource() {
  if (li_ != nullptr) {
    libinput_unref(li_);
  }
  if (udev_ != nullptr) {
    udev_unref(udev_);
  }
}

int LibinputInputSource::descriptor() const noexcept {
  return li_ != nullptr ? libinput_get_fd(li_) : -1;
}

void LibinputInputSource::push(const TouchEvent& event) noexcept {
  const std::size_t next = (tail_ + 1) % kRing;

  if (next == head_) {
    ++dropped_;
    // Losing an edge can leave a control pressed. Cancel the entire queued gesture.
    head_ = tail_ = 0;
    TouchEvent cancel = event;
    cancel.phase = TouchPhase::Cancel;
    ring_[tail_++] = cancel;
    static_cast<void>(contact_.cancel());
    return;
  }

  ring_[tail_] = event;
  tail_ = next;
}

void LibinputInputSource::handle_touch(libinput_event_type type, libinput_event_touch* touch) noexcept {
  if (type == LIBINPUT_EVENT_TOUCH_CANCEL) {
    if (const auto event = contact_.cancel()) {
      push(*event);
    }
    return;
  }

  const auto slot = libinput_event_touch_get_seat_slot(touch);
  std::optional<TouchEvent> event;

  if (type == LIBINPUT_EVENT_TOUCH_UP) {
    event = contact_.update(TouchPhase::Up, slot);
  } else if (type == LIBINPUT_EVENT_TOUCH_DOWN || type == LIBINPUT_EVENT_TOUCH_MOTION) {
    const Point2 point =
        to_logical({.x = static_cast<float>(libinput_event_touch_get_x_transformed(touch, native_.width)),
                    .y = static_cast<float>(libinput_event_touch_get_y_transformed(touch, native_.height))},
                   native_, rotation_);
    event = contact_.update(type == LIBINPUT_EVENT_TOUCH_DOWN ? TouchPhase::Down : TouchPhase::Move, slot, point.x,
                            point.y);
  }

  if (event) {
    push(*event);
  }
}

void LibinputInputSource::drain_device() noexcept {
  if (libinput_dispatch(li_) != 0) {
    return;
  }

  for (unsigned count = 0; count < 64; ++count) {
    libinput_event* ev = libinput_get_event(li_);
    if (ev == nullptr) {
      break;
    }
    const libinput_event_type type = libinput_event_get_type(ev);
    if (type == LIBINPUT_EVENT_TOUCH_DOWN || type == LIBINPUT_EVENT_TOUCH_MOTION || type == LIBINPUT_EVENT_TOUCH_UP ||
        type == LIBINPUT_EVENT_TOUCH_CANCEL) {
      handle_touch(type, libinput_event_get_touch_event(ev));
    }
    libinput_event_destroy(ev);
  }
}

bool LibinputInputSource::poll(TouchEvent& out) noexcept {
  if (head_ == tail_) {
    drain_device();
  }

  if (head_ == tail_) {
    return false;
  }

  out = ring_[head_];
  head_ = (head_ + 1) % kRing;

  return true;
}

}  // namespace c3::platform
