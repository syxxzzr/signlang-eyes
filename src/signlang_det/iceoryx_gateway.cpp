#include "iceoryx_gateway.hpp"

#include "common/ipc_utils.hpp"

#include <stdexcept>
#include <utility>

namespace signlang::signlang_det {

  auto IpcHandposeSubscriber::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    auto node_result =
        iox2::NodeBuilder().signal_handling_mode(iox2::SignalHandlingMode::Disabled).create<iox2::ServiceType::Ipc>();
    if (!node_result.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 node for handpose subscriber");
    }
    return std::move(node_result.value());
  }

  auto IpcHandposeSubscriber::create_subscriber(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                                const std::string& service_name, std::uint64_t buffer_size)
      -> iox2::Subscriber<iox2::ServiceType::Ipc, iox2::bb::Slice<handpose_det::HandPoseDetection>,
                          handpose_det::HandPoseFrameMetadata> {
    auto service_result = node.service_builder(signlang::common::ipc::service_name_from_string(service_name))
                              .publish_subscribe<iox2::bb::Slice<handpose_det::HandPoseDetection>>()
                              .user_header<handpose_det::HandPoseFrameMetadata>()
                              .open_or_create();

    if (!service_result.has_value()) {
      throw std::runtime_error("Failed to open handpose service: " + service_name);
    }

    auto subscriber_result = service_result.value().subscriber_builder().buffer_size(buffer_size).create();

    if (!subscriber_result.has_value()) {
      throw std::runtime_error("Failed to create handpose subscriber");
    }

    return std::move(subscriber_result.value());
  }

  IpcHandposeSubscriber::IpcHandposeSubscriber(const std::string& service_name, std::uint64_t subscriber_buffer_size) :
      node_(create_node()), subscriber_(create_subscriber(node_, service_name, subscriber_buffer_size)) {}

  auto IpcHandposeSubscriber::wait_for_work() -> bool {
    return node_.wait(iox2::bb::Duration::from_millis(5)).has_value();
  }

  auto IpcSignlangPublisher::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    auto node_result =
        iox2::NodeBuilder().signal_handling_mode(iox2::SignalHandlingMode::Disabled).create<iox2::ServiceType::Ipc>();
    if (!node_result.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 node for signlang publisher");
    }
    return std::move(node_result.value());
  }

  auto IpcSignlangPublisher::create_service(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                            const std::string& service_name) -> ResultService {
    auto service_result = node.service_builder(signlang::common::ipc::service_name_from_string(service_name))
                              .publish_subscribe<SignlangResult>()
                              .open_or_create();

    if (!service_result.has_value()) {
      throw std::runtime_error("Failed to open signlang result service: " + service_name);
    }
    return std::move(service_result.value());
  }

  auto IpcSignlangPublisher::create_publisher(const ResultService& service)
      -> iox2::Publisher<iox2::ServiceType::Ipc, SignlangResult, void> {
    auto publisher_result = service.publisher_builder().create();
    if (!publisher_result.has_value()) {
      throw std::runtime_error("Failed to create signlang result publisher");
    }

    return std::move(publisher_result.value());
  }

  IpcSignlangPublisher::IpcSignlangPublisher(const std::string& service_name) :
      node_(create_node()), service_(create_service(node_, service_name)), publisher_(create_publisher(service_)) {}

  auto IpcSignlangPublisher::has_subscribers() const -> bool {
    return signlang::common::ipc::has_subscribers(service_);
  }

  void IpcSignlangPublisher::publish(const SignlangResult& result) {
    auto loan_result = publisher_.loan_uninit();
    if (!loan_result.has_value()) {
      throw std::runtime_error("Failed to loan iceoryx2 signlang result sample");
    }

    auto loaned_sample = std::move(loan_result.value());
    auto result_copy = result;
    result_copy.sequence_number = sequence_number_++;

    auto initialized_sample = loaned_sample.write_payload(result_copy);
    const auto send_result = iox2::send(std::move(initialized_sample));
    if (!send_result.has_value()) {
      throw std::runtime_error("Failed to publish signlang result through iceoryx2");
    }
  }

  auto IpcPrototypeControlServer::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    iox2::set_log_level_from_env_or(iox2::LogLevel::Warn);

    auto node =
        iox2::NodeBuilder().signal_handling_mode(iox2::SignalHandlingMode::Disabled).create<iox2::ServiceType::Ipc>();
    if (!node.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 node for signlang prototype control server");
    }
    return std::move(node.value());
  }

  auto IpcPrototypeControlServer::create_service(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                                 const std::string& service_name)
      -> iox2::PortFactoryRequestResponse<iox2::ServiceType::Ipc, PrototypeControlRequest, void,
                                          PrototypeControlResponse, void> {
    auto service = node.service_builder(signlang::common::ipc::service_name_from_string(service_name))
                       .request_response<PrototypeControlRequest, PrototypeControlResponse>()
                       .max_servers(1)
                       .max_clients(4)
                       .max_active_requests_per_client(1)
                       .max_response_buffer_size(1)
                       .open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open or create signlang prototype control service: " + service_name);
    }
    return std::move(service.value());
  }

  auto IpcPrototypeControlServer::create_server(
      const iox2::PortFactoryRequestResponse<iox2::ServiceType::Ipc, PrototypeControlRequest, void,
                                             PrototypeControlResponse, void>& service)
      -> iox2::Server<iox2::ServiceType::Ipc, PrototypeControlRequest, void, PrototypeControlResponse, void> {
    auto server = service.server_builder().create();
    if (!server.has_value()) {
      throw std::runtime_error("Failed to create signlang prototype control server");
    }
    return std::move(server.value());
  }

  IpcPrototypeControlServer::IpcPrototypeControlServer(const std::string& service_name) :
      node_{create_node()}, service_{create_service(node_, service_name)}, server_{create_server(service_)} {}

} // namespace signlang::signlang_det
