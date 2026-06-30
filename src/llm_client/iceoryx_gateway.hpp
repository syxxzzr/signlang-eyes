#ifndef SIGNLANG_EYES_LLM_CLIENT_ICEORYX_GATEWAY_HPP
#define SIGNLANG_EYES_LLM_CLIENT_ICEORYX_GATEWAY_HPP

#include "llm_protocol.hpp"

#include "iox2/iceoryx2.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace signlang::llm_client {

  class IpcLlmServer {
  public:
    explicit IpcLlmServer(const std::string& service_name);

    IpcLlmServer(const IpcLlmServer&) = delete;
    auto operator=(const IpcLlmServer&) -> IpcLlmServer& = delete;
    IpcLlmServer(IpcLlmServer&&) = delete;
    auto operator=(IpcLlmServer&&) -> IpcLlmServer& = delete;

    template <typename Handler>
    void process_pending_requests(Handler&& handler);

    [[nodiscard]] auto wait_for_work(std::uint64_t timeout_ms) -> bool;

  private:
    using LlmService =
        iox2::PortFactoryRequestResponse<iox2::ServiceType::Ipc, LlmRequest, void, LlmResponse, void>;
    using LlmServer = iox2::Server<iox2::ServiceType::Ipc, LlmRequest, void, LlmResponse, void>;

    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_service(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> LlmService;
    static auto create_server(const LlmService& service) -> LlmServer;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    LlmService service_;
    LlmServer server_;
  };

  template <typename Handler>
  void IpcLlmServer::process_pending_requests(Handler&& handler) {
    auto receive_result = server_.receive();
    if (!receive_result.has_value()) {
      throw std::runtime_error("Failed to receive LLM request through iceoryx2");
    }

    auto active_request = std::move(receive_result.value());
    while (active_request.has_value()) {
      auto response = handler(active_request.value().payload());
      const auto send_result = active_request.value().send_copy(response);
      if (!send_result.has_value()) {
        throw std::runtime_error("Failed to send LLM response through iceoryx2");
      }

      receive_result = server_.receive();
      if (!receive_result.has_value()) {
        throw std::runtime_error("Failed to receive LLM request through iceoryx2");
      }
      active_request = std::move(receive_result.value());
    }
  }

} // namespace signlang::llm_client

#endif // SIGNLANG_EYES_LLM_CLIENT_ICEORYX_GATEWAY_HPP
