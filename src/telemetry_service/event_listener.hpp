#ifndef SIGNLANG_EYES_TELEMETRY_SERVICE_EVENT_LISTENER_HPP
#define SIGNLANG_EYES_TELEMETRY_SERVICE_EVENT_LISTENER_HPP

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace signlang::telemetry_service {

  struct AlertEvent {
    std::uint64_t id;
    std::uint64_t count;
  };

  class EventListener {
  public:
    using Callback = std::function<void(const AlertEvent& event)>;

    EventListener(std::string service_name, std::string node_name, Callback callback);
    ~EventListener();

    EventListener(const EventListener&) = delete;
    auto operator=(const EventListener&) -> EventListener& = delete;
    EventListener(EventListener&&) = delete;
    auto operator=(EventListener&&) -> EventListener& = delete;

    void start();
    void stop();

  private:
    void run();

    std::string service_name_;
    std::string node_name_;
    Callback callback_;
    std::atomic_bool running_{false};
    std::atomic_bool stop_requested_{false};
    std::thread thread_;
  };

} // namespace signlang::telemetry_service

#endif // SIGNLANG_EYES_TELEMETRY_SERVICE_EVENT_LISTENER_HPP
