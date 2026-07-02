#include "iceoryx_gateway.hpp"

#include "common/ipc_utils.hpp"
#include "iox2/bb/duration.hpp"
#include "iox2/bb/static_function.hpp"
#include "iox2/event_id.hpp"
#include "spdlog/spdlog.h"

#include <chrono>
#include <exception>
#include <stdexcept>
#include <thread>
#include <utility>

namespace signlang::peripheral_service {

  IpcDisplayServer::IpcDisplayServer(const std::string& service_name) :
      node_{create_node()}, service_{create_service(node_, service_name)}, server_{create_server(service_)} {}

  auto IpcDisplayServer::wait_for_work(std::uint64_t timeout_ms) -> bool {
    return node_.wait(iox2::bb::Duration::from_millis(timeout_ms)).has_value();
  }

  auto IpcDisplayServer::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    iox2::set_log_level_from_env_or(iox2::LogLevel::Warn);
    auto node =
        iox2::NodeBuilder().signal_handling_mode(iox2::SignalHandlingMode::Disabled).create<iox2::ServiceType::Ipc>();
    if (!node.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 node for peripheral display server");
    }
    return std::move(node.value());
  }

  auto IpcDisplayServer::create_service(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                        const std::string& service_name) -> DisplayService {
    auto service = node.service_builder(signlang::common::ipc::service_name_from_string(service_name))
                       .request_response<DisplayRequest, DisplayResponse>()
                       .max_servers(1)
                       .max_clients(8)
                       .max_active_requests_per_client(1)
                       .max_response_buffer_size(1)
                       .open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open or create peripheral display request-response service: " +
                               service_name);
    }
    return std::move(service.value());
  }

  auto IpcDisplayServer::create_server(const DisplayService& service) -> DisplayServer {
    auto server = service.server_builder().create();
    if (!server.has_value()) {
      throw std::runtime_error("Failed to create peripheral display request-response server");
    }
    return std::move(server.value());
  }

  IpcStateWatcher::IpcStateWatcher(std::string event_service_name, std::string blackboard_service_name,
                                   Callback callback) :
      event_service_name_{std::move(event_service_name)},
      blackboard_service_name_{std::move(blackboard_service_name)}, callback_{std::move(callback)} {}

  IpcStateWatcher::~IpcStateWatcher() { stop(); }

  void IpcStateWatcher::start() {
    if (running_.exchange(true)) {
      return;
    }
    stop_requested_.store(false, std::memory_order_release);
    thread_ = std::thread{&IpcStateWatcher::run, this};
  }

  void IpcStateWatcher::stop() {
    stop_requested_.store(true, std::memory_order_release);
    if (thread_.joinable()) {
      thread_.join();
    }
    running_.store(false, std::memory_order_release);
  }

  void IpcStateWatcher::run() {
    try {
      iox2::set_log_level_from_env_or(iox2::LogLevel::Warn);
      auto node =
          iox2::NodeBuilder().signal_handling_mode(iox2::SignalHandlingMode::Disabled).create<iox2::ServiceType::Ipc>();
      if (!node.has_value()) {
        throw std::runtime_error("Failed to create iceoryx2 node for peripheral state watcher");
      }

      auto blackboard = node.value()
                            .service_builder(signlang::common::ipc::service_name_from_string(blackboard_service_name_))
                            .blackboard_opener<signlang::state_machine::AppStateKey>()
                            .open();
      while (!blackboard.has_value() && !stop_requested_.load(std::memory_order_acquire)) {
        spdlog::warn("app state blackboard service '{}' is not available; retrying", blackboard_service_name_);
        std::this_thread::sleep_for(std::chrono::milliseconds{200});
        blackboard = node.value()
                         .service_builder(signlang::common::ipc::service_name_from_string(blackboard_service_name_))
                         .blackboard_opener<signlang::state_machine::AppStateKey>()
                         .open();
      }
      if (!blackboard.has_value()) {
        running_.store(false, std::memory_order_release);
        return;
      }

      auto reader = blackboard.value().reader_builder().create();
      if (!reader.has_value()) {
        throw std::runtime_error("Failed to create app state blackboard reader");
      }

      auto entry = reader.value().entry<signlang::state_machine::AppState>(signlang::state_machine::default_app_state_key());
      if (!entry.has_value()) {
        throw std::runtime_error("Failed to create app state blackboard entry reader");
      }

      auto event_service = node.value()
                               .service_builder(signlang::common::ipc::service_name_from_string(event_service_name_))
                               .event()
                               .open_or_create();
      if (!event_service.has_value()) {
        throw std::runtime_error("Failed to open or create app state event service: " + event_service_name_);
      }

      auto listener = event_service.value().listener_builder().create();
      if (!listener.has_value()) {
        throw std::runtime_error("Failed to create app state event listener for peripheral service");
      }

      if (callback_) {
        callback_(*entry.value().get());
      }

      spdlog::info("peripheral state watcher listening on '{}'", event_service_name_);
      while (!stop_requested_.load(std::memory_order_acquire)) {
        iox2::bb::StaticFunction<void(iox2::EventId)> callback{[&](iox2::EventId /* event_id */) {
          if (callback_) {
            callback_(*entry.value().get());
          }
        }};
        const auto result = listener.value().timed_wait_all(callback, iox2::bb::Duration::from_millis(200));
        if (!result.has_value()) {
          spdlog::warn("peripheral state watcher wait failed");
        }
      }
    } catch (const std::exception& error) {
      spdlog::error("peripheral state watcher stopped: {}", error.what());
    }
    running_.store(false, std::memory_order_release);
  }

  IpcStateControlClient::IpcStateControlClient(const std::string& service_name) :
      node_{create_node()}, service_{create_service(node_, service_name)}, client_{create_client(service_)} {}

  void IpcStateControlClient::request_next_base_state() const {
    const auto request = signlang::state_machine::StateControlRequest{
        signlang::state_machine::StateControlCommand::NextBase,
        signlang::state_machine::AppState::Normal,
        0};
    (void)client_.send_copy(request);
  }

  auto IpcStateControlClient::has_server() const -> bool { return signlang::common::ipc::has_servers(service_); }

  auto IpcStateControlClient::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    iox2::set_log_level_from_env_or(iox2::LogLevel::Warn);
    auto node =
        iox2::NodeBuilder().signal_handling_mode(iox2::SignalHandlingMode::Disabled).create<iox2::ServiceType::Ipc>();
    if (!node.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 node for peripheral state control client");
    }
    return std::move(node.value());
  }

  auto IpcStateControlClient::create_service(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                             const std::string& service_name) -> StateControlService {
    auto service = node.service_builder(signlang::common::ipc::service_name_from_string(service_name))
                       .request_response<signlang::state_machine::StateControlRequest,
                                         signlang::state_machine::StateControlResponse>()
                       .max_servers(1)
                       .max_clients(8)
                       .max_active_requests_per_client(1)
                       .max_response_buffer_size(1)
                       .open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open or create app state control service: " + service_name);
    }
    return std::move(service.value());
  }

  auto IpcStateControlClient::create_client(const StateControlService& service) -> StateControlClient {
    auto client = service.client_builder().create();
    if (!client.has_value()) {
      throw std::runtime_error("Failed to create app state control client for peripheral service");
    }
    return std::move(client.value());
  }

  IpcAlertNotifier::IpcAlertNotifier(const std::string& service_name) :
      node_{create_node()}, service_{create_service(node_, service_name)}, notifier_{create_notifier(service_)} {}

  void IpcAlertNotifier::notify_alert() const {
    const auto result = notifier_.notify_with_custom_event_id(iox2::EventId{next_event_id_++});
    if (!result.has_value()) {
      spdlog::warn("failed to notify position alert event");
    }
  }

  auto IpcAlertNotifier::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    iox2::set_log_level_from_env_or(iox2::LogLevel::Warn);
    auto node =
        iox2::NodeBuilder().signal_handling_mode(iox2::SignalHandlingMode::Disabled).create<iox2::ServiceType::Ipc>();
    if (!node.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 node for peripheral alert notifier");
    }
    return std::move(node.value());
  }

  auto IpcAlertNotifier::create_service(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                        const std::string& service_name)
      -> iox2::PortFactoryEvent<iox2::ServiceType::Ipc> {
    auto service =
        node.service_builder(signlang::common::ipc::service_name_from_string(service_name)).event().open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open or create position alert event service: " + service_name);
    }
    return std::move(service.value());
  }

  auto IpcAlertNotifier::create_notifier(const iox2::PortFactoryEvent<iox2::ServiceType::Ipc>& service)
      -> iox2::Notifier<iox2::ServiceType::Ipc> {
    auto notifier = service.notifier_builder().create();
    if (!notifier.has_value()) {
      throw std::runtime_error("Failed to create position alert event notifier");
    }
    return std::move(notifier.value());
  }

} // namespace signlang::peripheral_service
