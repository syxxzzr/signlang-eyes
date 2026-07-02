#include "iceoryx_gateway.hpp"

#include "common/ipc_utils.hpp"
#include "spdlog/spdlog.h"

#include <stdexcept>
#include <utility>

namespace signlang::peripheral_service {

  IpcDisplayServer::IpcDisplayServer(const std::string& service_name) :
      node_{create_node()}, service_{create_service(node_, service_name)}, server_{create_server(service_)} {}

  auto IpcDisplayServer::wait_for_work(std::uint64_t timeout_ms) -> bool {
    return node_.wait(iox2::bb::Duration::from_millis(timeout_ms)).has_value();
  }

  auto IpcDisplayServer::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    return signlang::common::ipc::create_ipc_node("Failed to create iceoryx2 node for peripheral display server");
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

  IpcStateControlClient::IpcStateControlClient(const std::string& service_name) :
      node_{create_node()}, service_{create_service(node_, service_name)}, client_{create_client(service_)} {}

  void IpcStateControlClient::request_next_base_state() const {
    const auto request = signlang::state_machine::StateControlRequest{
        signlang::state_machine::StateControlCommand::NextBase,
        signlang::state_machine::AppState::Normal,
        0};
    (void)client_.send_copy(request);
    spdlog::info("sent peripheral state-control NextBase request");
  }

  auto IpcStateControlClient::has_server() const -> bool { return signlang::common::ipc::has_servers(service_); }

  auto IpcStateControlClient::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    return signlang::common::ipc::create_ipc_node(
        "Failed to create iceoryx2 node for peripheral state control client");
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
      return;
    }
    spdlog::info("notified position alert event");
  }

  auto IpcAlertNotifier::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    return signlang::common::ipc::create_ipc_node("Failed to create iceoryx2 node for peripheral alert notifier");
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
