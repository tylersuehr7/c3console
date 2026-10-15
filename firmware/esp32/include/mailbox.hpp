#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <array>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace hw {

// ESP32's libstdc++ atomics are not guaranteed lock-free. These fixed-storage RTOS
// queues use short critical sections; callers never wait for capacity or a message.
template <typename T, std::size_t Capacity>
class Mailbox {
  static_assert(std::is_trivially_copyable_v<T>);

public:
  Mailbox() : queue_(xQueueCreateStatic(Capacity, sizeof(T), storage_.data(), &control_)) {}
  Mailbox(const Mailbox&) = delete;
  Mailbox& operator=(const Mailbox&) = delete;

  [[nodiscard]] bool push(const T& value) noexcept { return xQueueSend(queue_, &value, 0) == pdTRUE; }

  [[nodiscard]] std::optional<T> pop() noexcept {
    T value;
    if (xQueueReceive(queue_, &value, 0) == pdTRUE) {
      return value;
    }
    return std::nullopt;
  }

private:
  StaticQueue_t control_{};
  std::array<std::uint8_t, Capacity * sizeof(T)> storage_{};
  QueueHandle_t queue_;
};

}  // namespace hw
