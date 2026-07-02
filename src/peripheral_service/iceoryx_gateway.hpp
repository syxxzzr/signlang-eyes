#ifndef SIGNLANG_EYES_PERIPHERAL_SERVICE_ICEORYX_GATEWAY_HPP
#define SIGNLANG_EYES_PERIPHERAL_SERVICE_ICEORYX_GATEWAY_HPP

#include "peripheral_protocol.hpp"

#include "iox2/iceoryx2.hpp"
#include "state_machine/state_control.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace signlang::peripheral_service {

  class IpcDisplayServer {
  public:
    explicit IpcDisplayServer(const std::string& service_name);

    IpcDisplayServer(const IpcDisplayServer&) = delete;
    auto operator=(const IpcDisplayServer&) -> IpcDisplayServer& = delete;
    IpcDisplayServer(IpcDisplayServer&&) = delete;
    auto operator=(IpcDisplayServer&&) -> IpcDisplayServer& = delete;

    template <typename Handler>
    void process_pending_requests(Handler&& handler);

    [[nodiscard]] auto wait_for_work(std::uint64_t timeout_ms) -> bool;

  private:
    using DisplayService =
        iox2::PortFactoryRequestResponse<iox2::ServiceType::Ipc, DisplayRequest, void, DisplayResponse, void>;
    using DisplayServer = iox2::Server<iox2::ServiceType::Ipc, DisplayRequest, void, DisplayResponse, void>;

    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_service(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> DisplayService;
    static auto create_server(const DisplayService& service) -> DisplayServer;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    DisplayService service_;
    DisplayServer server_;
  };

  class IpcStateControlClient {
  public:
    explicit IpcStateControlClient(const std::string& service_name);

    IpcStateControlClient(const IpcStateControlClient&) = delete;
    auto operator=(const IpcStateControlClient&) -> IpcStateControlClient& = delete;
    IpcStateControlClient(IpcStateControlClient&&) = delete;
    auto operator=(IpcStateControlClient&&) -> IpcStateControlClient& = delete;

    void request_next_base_state() const;
    [[nodiscard]] auto has_server() const -> bool;

  private:
    using StateControlService =
        iox2::PortFactoryRequestResponse<iox2::ServiceType::Ipc, signlang::state_machine::StateControlRequest, void,
                                         signlang::state_machine::StateControlResponse, void>;
    using StateControlClient = iox2::Client<iox2::ServiceType::Ipc, signlang::state_machine::StateControlRequest, void,
                                            signlang::state_machine::StateControlResponse, void>;

    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_service(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> StateControlService;
    static auto create_client(const StateControlService& service) -> StateControlClient;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    StateControlService service_;
    StateControlClient client_;
  };

  class IpcAlertNotifier {
  public:
    explicit IpcAlertNotifier(const std::string& service_name);

    IpcAlertNotifier(const IpcAlertNotifier&) = delete;
    auto operator=(const IpcAlertNotifier&) -> IpcAlertNotifier& = delete;
    IpcAlertNotifier(IpcAlertNotifier&&) = delete;
    auto operator=(IpcAlertNotifier&&) -> IpcAlertNotifier& = delete;

    void notify_alert() const;

  private:
    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_service(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> iox2::PortFactoryEvent<iox2::ServiceType::Ipc>;
    static auto create_notifier(const iox2::PortFactoryEvent<iox2::ServiceType::Ipc>& service)
        -> iox2::Notifier<iox2::ServiceType::Ipc>;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    iox2::PortFactoryEvent<iox2::ServiceType::Ipc> service_;
    iox2::Notifier<iox2::ServiceType::Ipc> notifier_;
    mutable std::uint64_t next_event_id_{1};
  };

  template <typename Handler>
  void IpcDisplayServer::process_pending_requests(Handler&& handler) {
    auto receive_result = server_.receive();
    if (!receive_result.has_value()) {
      throw std::runtime_error("Failed to receive peripheral display request through iceoryx2");
    }

    auto active_request = std::move(receive_result.value());
    while (active_request.has_value()) {
      auto response = handler(active_request.value().payload());
      const auto send_result = active_request.value().send_copy(response);
      if (!send_result.has_value()) {
        throw std::runtime_error("Failed to send peripheral display response through iceoryx2");
      }

      receive_result = server_.receive();
      if (!receive_result.has_value()) {
        throw std::runtime_error("Failed to receive peripheral display request through iceoryx2");
      }
      active_request = std::move(receive_result.value());
    }
  }

} // namespace signlang::peripheral_service

#endif // SIGNLANG_EYES_PERIPHERAL_SERVICE_ICEORYX_GATEWAY_HPP
