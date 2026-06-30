#include "event_listener.hpp"

#include "common/ipc_utils.hpp"
#include "iox2/bb/duration.hpp"
#include "iox2/bb/static_function.hpp"
#include "iox2/iceoryx2.hpp"
#include "spdlog/spdlog.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace signlang::position_service {

  EventListener::EventListener(std::string service_name, std::string node_name, Callback callback) :
      service_name_{std::move(service_name)}, node_name_{std::move(node_name)}, callback_{std::move(callback)} {}

  EventListener::~EventListener() { stop(); }

  void EventListener::start() {
    if (service_name_.empty() || running_.exchange(true)) {
      return;
    }

    stop_requested_.store(false, std::memory_order_release);
    thread_ = std::thread{&EventListener::run, this};
  }

  void EventListener::stop() {
    stop_requested_.store(true, std::memory_order_release);
    if (thread_.joinable()) {
      thread_.join();
    }
    running_.store(false, std::memory_order_release);
  }

  void EventListener::run() {
    try {
      iox2::set_log_level_from_env_or(iox2::LogLevel::Warn);

      auto node_name = iox2::NodeName::create(node_name_.c_str());
      if (!node_name.has_value()) {
        throw std::runtime_error("Failed to create iceoryx2 alert listener node name: " + node_name_);
      }

      auto node = iox2::NodeBuilder{}
                      .name(std::move(node_name.value()))
                      .signal_handling_mode(iox2::SignalHandlingMode::Disabled)
                      .create<iox2::ServiceType::Ipc>();
      if (!node.has_value()) {
        throw std::runtime_error("Failed to create iceoryx2 alert listener node");
      }

      auto service = node.value()
                         .service_builder(signlang::common::ipc::service_name_from_string(service_name_))
                         .event()
                         .open_or_create();
      if (!service.has_value()) {
        throw std::runtime_error("Failed to open or create iceoryx2 alert event service: " + service_name_);
      }

      auto listener = service.value().listener_builder().create();
      if (!listener.has_value()) {
        throw std::runtime_error("Failed to create iceoryx2 alert event listener");
      }

      spdlog::info("Alert event listener started on service '{}'", service_name_);

      while (!stop_requested_.load(std::memory_order_acquire)) {
        iox2::bb::StaticFunction<void(iox2::EventActivation)> callback{[this](iox2::EventActivation activation) {
          if (callback_) {
            callback_(AlertEvent{
                .id = static_cast<std::uint64_t>(activation.id().as_value()),
                .count = activation.count(),
            });
          }
        }};

        const auto result = listener.value().timed_wait(callback, iox2::bb::Duration::from_millis(200));
        if (!result.has_value()) {
          spdlog::warn("Alert event listener wait failed");
        }
      }
    } catch (const std::exception& error) {
      spdlog::error("Alert event listener stopped: {}", error.what());
    }

    running_.store(false, std::memory_order_release);
  }

} // namespace signlang::position_service
