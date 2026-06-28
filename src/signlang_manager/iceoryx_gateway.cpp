#include "iceoryx_gateway.hpp"

#include "common/ipc_utils.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace signlang::signlang_manager {

  IpcHandposeSubscriber::IpcHandposeSubscriber(const std::string& service_name, std::uint64_t subscriber_buffer_size) :
      node_{create_node()}, subscriber_{create_subscriber(node_, service_name, subscriber_buffer_size)} {}

  auto IpcHandposeSubscriber::wait_for_work() -> bool {
    return node_.wait(iox2::bb::Duration::from_millis(5)).has_value();
  }

  auto IpcHandposeSubscriber::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    iox2::set_log_level_from_env_or(iox2::LogLevel::Warn);

    auto node =
        iox2::NodeBuilder().signal_handling_mode(iox2::SignalHandlingMode::Disabled).create<iox2::ServiceType::Ipc>();
    if (!node.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 node for signlang manager handpose subscriber");
    }
    return std::move(node.value());
  }

  auto IpcHandposeSubscriber::create_subscriber(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                                const std::string& service_name, std::uint64_t buffer_size)
      -> iox2::Subscriber<iox2::ServiceType::Ipc, iox2::bb::Slice<handpose_det::HandPoseDetection>,
                          handpose_det::HandPoseFrameMetadata> {
    auto service = node.service_builder(signlang::common::ipc::service_name_from_string(service_name))
                       .publish_subscribe<iox2::bb::Slice<handpose_det::HandPoseDetection>>()
                       .user_header<handpose_det::HandPoseFrameMetadata>()
                       .open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open handpose service in signlang manager: " + service_name);
    }

    auto subscriber = service.value().subscriber_builder().buffer_size(buffer_size).create();
    if (!subscriber.has_value()) {
      throw std::runtime_error("Failed to create signlang manager handpose subscriber");
    }
    return std::move(subscriber.value());
  }

  IpcSignlangResultSubscriber::IpcSignlangResultSubscriber(const std::string& service_name,
                                                           std::uint64_t subscriber_buffer_size) :
      node_{create_node()},
      subscriber_{create_subscriber(node_, service_name, subscriber_buffer_size)} {}

  auto IpcSignlangResultSubscriber::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    iox2::set_log_level_from_env_or(iox2::LogLevel::Warn);

    auto node =
        iox2::NodeBuilder().signal_handling_mode(iox2::SignalHandlingMode::Disabled).create<iox2::ServiceType::Ipc>();
    if (!node.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 node for signlang manager result subscriber");
    }
    return std::move(node.value());
  }

  auto IpcSignlangResultSubscriber::create_subscriber(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                                      const std::string& service_name, std::uint64_t buffer_size)
      -> iox2::Subscriber<iox2::ServiceType::Ipc, signlang_det::SignlangResult, void> {
    auto service = node.service_builder(signlang::common::ipc::service_name_from_string(service_name))
                       .publish_subscribe<signlang_det::SignlangResult>()
                       .open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open signlang result service in signlang manager: " + service_name);
    }

    auto subscriber = service.value().subscriber_builder().buffer_size(buffer_size).create();
    if (!subscriber.has_value()) {
      throw std::runtime_error("Failed to create signlang manager result subscriber");
    }
    return std::move(subscriber.value());
  }

  IpcPrototypeControlClient::IpcPrototypeControlClient(const std::string& service_name) :
      node_{create_node()}, service_{create_service(node_, service_name)}, client_{create_client(service_)} {}

  auto IpcPrototypeControlClient::request_reload() const -> signlang_det::PrototypeControlResponse {
    static auto request_id = std::uint32_t{0};
    const auto request = signlang_det::PrototypeControlRequest{
        .command = signlang_det::PrototypeControlCommand::ReloadPrototypes,
        .request_id = request_id++,
    };
    auto send_result = client_.send_copy(request);
    if (!send_result.has_value()) {
      throw std::runtime_error("Failed to send signlang prototype reload request");
    }

    auto pending_response = std::move(send_result.value());
    if (pending_response.number_of_server_connections() == 0) {
      throw std::runtime_error("No signlang prototype control server received reload request");
    }

    constexpr auto kMaxWaitAttempts = 100;
    for (auto attempt = 0; attempt < kMaxWaitAttempts; ++attempt) {
      auto receive_result = pending_response.receive();
      if (!receive_result.has_value()) {
        throw std::runtime_error("Failed to receive signlang prototype reload response");
      }

      if (receive_result.value().has_value()) {
        const auto& response = receive_result.value().value().payload();
        if (response.request_id != request.request_id) {
          throw std::runtime_error("Received mismatched signlang prototype reload response");
        }
        if (response.status != signlang_det::PrototypeControlStatus::Ok) {
          const auto message_end = std::find(response.message.begin(), response.message.end(), '\0');
          const auto message = std::string{response.message.begin(), message_end};
          throw std::runtime_error("Signlang prototype reload failed" +
                                   (message.empty() ? std::string{} : ": " + message));
        }
        return response;
      }

      (void)node_.wait(iox2::bb::Duration::from_millis(10));
    }

    throw std::runtime_error("Timed out waiting for signlang prototype reload response");
  }

  auto IpcPrototypeControlClient::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    iox2::set_log_level_from_env_or(iox2::LogLevel::Warn);

    auto node =
        iox2::NodeBuilder().signal_handling_mode(iox2::SignalHandlingMode::Disabled).create<iox2::ServiceType::Ipc>();
    if (!node.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 node for signlang manager prototype control client");
    }
    return std::move(node.value());
  }

  auto IpcPrototypeControlClient::create_service(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                                 const std::string& service_name) -> PrototypeControlService {
    auto service =
        node.service_builder(signlang::common::ipc::service_name_from_string(service_name))
            .request_response<signlang_det::PrototypeControlRequest, signlang_det::PrototypeControlResponse>()
            .max_servers(1)
            .max_clients(4)
            .max_active_requests_per_client(1)
            .max_response_buffer_size(1)
            .open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open signlang prototype control service in manager: " + service_name);
    }
    return std::move(service.value());
  }

  auto IpcPrototypeControlClient::create_client(const PrototypeControlService& service) -> PrototypeControlClient {
    auto client = service.client_builder().create();
    if (!client.has_value()) {
      throw std::runtime_error("Failed to create signlang prototype control client in manager");
    }
    return std::move(client.value());
  }

} // namespace signlang::signlang_manager
