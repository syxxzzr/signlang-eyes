#include "iceoryx_gateway.hpp"

#include <stdexcept>
#include <utility>

namespace signlang::signlang_manager {
  namespace {

    auto service_name_from_string(const std::string& service_name) -> iox2::ServiceName {
      const auto parsed_service_name = iox2::ServiceName::create(service_name.c_str());
      if (!parsed_service_name.has_value()) {
        throw std::runtime_error("Invalid iceoryx2 service name: " + service_name);
      }
      return parsed_service_name.value();
    }

  } // namespace

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
    auto service = node.service_builder(service_name_from_string(service_name))
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

  IpcPrototypeControlClient::IpcPrototypeControlClient(const std::string& service_name) :
      node_{create_node()}, service_{create_service(node_, service_name)}, client_{create_client(service_)} {}

  void IpcPrototypeControlClient::request_reload() const {
    static auto request_id = std::uint32_t{0};
    const auto request = signlang_det::PrototypeControlRequest{
        .command = signlang_det::PrototypeControlCommand::ReloadPrototypes,
        .request_id = request_id++,
    };
    (void)client_.send_copy(request);
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
        node.service_builder(service_name_from_string(service_name))
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
