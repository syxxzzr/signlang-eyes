#ifndef SIGNLANG_EYES_POSITION_SERVICE_PAYLOAD_QUEUE_HPP
#define SIGNLANG_EYES_POSITION_SERVICE_PAYLOAD_QUEUE_HPP

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

namespace signlang::position_service {

  template <std::size_t Capacity>
  class PayloadQueue {
  public:
    static_assert(Capacity > 1);

    PayloadQueue() = default;
    PayloadQueue(const PayloadQueue&) = delete;
    auto operator=(const PayloadQueue&) -> PayloadQueue& = delete;

    [[nodiscard]] auto push(std::string payload) -> bool {
      const auto head = head_.load(std::memory_order_relaxed);
      const auto next_head = increment(head);
      if (next_head == tail_.load(std::memory_order_acquire)) {
        return false;
      }

      slots_[head] = std::move(payload);
      head_.store(next_head, std::memory_order_release);
      return true;
    }

    [[nodiscard]] auto pop() -> std::optional<std::string> {
      const auto tail = tail_.load(std::memory_order_relaxed);
      if (tail == head_.load(std::memory_order_acquire)) {
        return std::nullopt;
      }

      auto payload = std::move(slots_[tail]);
      slots_[tail].clear();
      tail_.store(increment(tail), std::memory_order_release);
      return payload;
    }

  private:
    [[nodiscard]] static constexpr auto increment(std::size_t index) -> std::size_t {
      return (index + 1) % Capacity;
    }

    std::array<std::string, Capacity> slots_{};
    alignas(64) std::atomic_size_t head_{0};
    alignas(64) std::atomic_size_t tail_{0};
  };

} // namespace signlang::position_service

#endif // SIGNLANG_EYES_POSITION_SERVICE_PAYLOAD_QUEUE_HPP
