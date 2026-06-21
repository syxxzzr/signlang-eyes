#include "iceoryx_gateway.hpp"

#include "iox2/service_builder_blackboard_error.hpp"
#include "spdlog/spdlog.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace signlang::state_machine {
  namespace {

    auto service_name_from_string(const std::string& service_name) -> iox2::ServiceName {
      const auto parsed_service_name = iox2::ServiceName::create(service_name.c_str());
      if (!parsed_service_name.has_value()) {
        throw std::runtime_error("Invalid iceoryx2 service name: " + service_name);
      }

      return parsed_service_name.value();
    }

  } // namespace

  IpcStatePublisher::IpcStatePublisher(const std::string& event_service_name,
                                       const std::string& blackboard_service_name, AppState initial_state) :
      node_{create_node()},
      blackboard_service_{create_blackboard_service(node_, blackboard_service_name, initial_state)},
      writer_{create_writer(blackboard_service_)}, state_entry_{create_state_entry(writer_)},
      event_service_{create_event_service(node_, event_service_name)}, notifier_{create_notifier(event_service_)},
      current_state_{initial_state} {
    publish_current_state();
  }

  auto IpcStatePublisher::current_state() const -> AppState { return current_state_; }

  void IpcStatePublisher::set_state(AppState state) {
    if (state == current_state_) {
      return;
    }

    spdlog::info("State transition: {} -> {}", app_state_name(current_state_), app_state_name(state));
    current_state_ = state;
    publish_current_state();
  }

  auto IpcStatePublisher::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    iox2::set_log_level_from_env_or(iox2::LogLevel::Warn);

    auto node = iox2::NodeBuilder()
                    .signal_handling_mode(iox2::SignalHandlingMode::Disabled)
                    .create<iox2::ServiceType::Ipc>();
    if (!node.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 IPC state machine node");
    }

    return std::move(node.value());
  }

  auto IpcStatePublisher::create_blackboard_service(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                                    const std::string& service_name, AppState initial_state)
      -> iox2::PortFactoryBlackboard<iox2::ServiceType::Ipc, AppStateKey> {
    const auto parsed_service_name = service_name_from_string(service_name);
    auto service = node.service_builder(parsed_service_name)
                       .blackboard_creator<AppStateKey>()
                       .add<AppState>(default_app_state_key(), initial_state)
                       .create();
    if (service.has_value()) {
      return std::move(service.value());
    }

    const auto create_error = service.error();
    if (create_error != iox2::BlackboardCreateError::AlreadyExists) {
      throw std::runtime_error("Failed to create iceoryx2 app state blackboard service: " + service_name);
    }

    auto opened_service = node.service_builder(parsed_service_name).blackboard_opener<AppStateKey>().open();
    if (!opened_service.has_value()) {
      throw std::runtime_error("Failed to open existing iceoryx2 app state blackboard service: " + service_name);
    }

    return std::move(opened_service.value());
  }

  auto IpcStatePublisher::create_writer(const iox2::PortFactoryBlackboard<iox2::ServiceType::Ipc, AppStateKey>& service)
      -> iox2::Writer<iox2::ServiceType::Ipc, AppStateKey> {
    auto writer = service.writer_builder().create();
    if (!writer.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 app state blackboard writer");
    }

    return std::move(writer.value());
  }

  auto IpcStatePublisher::create_state_entry(iox2::Writer<iox2::ServiceType::Ipc, AppStateKey>& writer)
      -> iox2::EntryHandleMut<iox2::ServiceType::Ipc, AppStateKey, AppState> {
    auto entry = writer.entry<AppState>(default_app_state_key());
    if (!entry.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 app state blackboard entry writer");
    }

    return std::move(entry.value());
  }

  auto IpcStatePublisher::create_event_service(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                               const std::string& service_name)
      -> iox2::PortFactoryEvent<iox2::ServiceType::Ipc> {
    auto service = node.service_builder(service_name_from_string(service_name)).event().open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open or create iceoryx2 app state event service: " + service_name);
    }

    return std::move(service.value());
  }

  auto IpcStatePublisher::create_notifier(const iox2::PortFactoryEvent<iox2::ServiceType::Ipc>& service)
      -> iox2::Notifier<iox2::ServiceType::Ipc> {
    auto notifier = service.notifier_builder().create();
    if (!notifier.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 app state event notifier");
    }

    return std::move(notifier.value());
  }

  void IpcStatePublisher::publish_current_state() {
    state_entry_.update_with_copy(current_state_);

    const auto notify_result = notifier_.notify_with_custom_event_id(state_entry_.entry_id());
    if (!notify_result.has_value()) {
      throw std::runtime_error("Failed to notify app state change through iceoryx2");
    }
  }

  IpcStateControlServer::IpcStateControlServer(const std::string& service_name) :
      node_{create_node()}, service_{create_service(node_, service_name)}, server_{create_server(service_)} {}

  void IpcStateControlServer::process_pending_requests(StateController& controller, IpcStatePublisher& publisher,
                                                       StateController::Clock::time_point now) {
    auto receive_result = server_.receive();
    if (!receive_result.has_value()) {
      throw std::runtime_error("Failed to receive iceoryx2 app state control request");
    }

    auto active_request = std::move(receive_result.value());
    while (active_request.has_value()) {
      const auto previous_state = controller.current_published_state();
      const auto response = controller.handle_request(active_request.value().payload(), now);
      if (controller.current_published_state() != previous_state) {
        publisher.set_state(controller.current_published_state());
      }

      const auto send_result = active_request.value().send_copy(response);
      if (!send_result.has_value()) {
        throw std::runtime_error("Failed to send iceoryx2 app state control response");
      }

      receive_result = server_.receive();
      if (!receive_result.has_value()) {
        throw std::runtime_error("Failed to receive iceoryx2 app state control request");
      }
      active_request = std::move(receive_result.value());
    }
  }

  auto IpcStateControlServer::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    iox2::set_log_level_from_env_or(iox2::LogLevel::Warn);

    auto node = iox2::NodeBuilder()
                    .signal_handling_mode(iox2::SignalHandlingMode::Disabled)
                    .create<iox2::ServiceType::Ipc>();
    if (!node.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 IPC state control node");
    }

    return std::move(node.value());
  }

  auto IpcStateControlServer::create_service(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                             const std::string& service_name)
      -> iox2::PortFactoryRequestResponse<iox2::ServiceType::Ipc, StateControlRequest, void, StateControlResponse,
                                          void> {
    auto service = node.service_builder(service_name_from_string(service_name))
                       .request_response<StateControlRequest, StateControlResponse>()
                       .max_servers(1)
                       .max_clients(8)
                       .max_active_requests_per_client(1)
                       .max_response_buffer_size(1)
                       .open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open or create iceoryx2 app state control request-response service: " +
                               service_name);
    }

    return std::move(service.value());
  }

  auto IpcStateControlServer::create_server(
      const iox2::PortFactoryRequestResponse<iox2::ServiceType::Ipc, StateControlRequest, void, StateControlResponse,
                                             void>& service)
      -> iox2::Server<iox2::ServiceType::Ipc, StateControlRequest, void, StateControlResponse, void> {
    auto server = service.server_builder().create();
    if (!server.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 app state control server");
    }

    return std::move(server.value());
  }

} // namespace signlang::state_machine
