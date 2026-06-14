#include "iceoryx_gateway.hpp"

#include <stdexcept>
#include <utility>

namespace signlang::signlang_det {
  namespace {

    auto service_name_from_string(const std::string& name) -> iox2::ServiceName {
      auto result = iox2::ServiceName::create(name.c_str());
      if (!result.has_value()) {
        throw std::runtime_error("Invalid iceoryx2 service name: " + name);
      }
      return std::move(result.value());
    }

  } // namespace

  auto IpcHandposeSubscriber::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    auto node_result = iox2::NodeBuilder()
      .create<iox2::ServiceType::Ipc>();
    if (!node_result.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 node for handpose subscriber");
    }
    return std::move(node_result.value());
  }

  auto IpcHandposeSubscriber::create_subscriber(
    const iox2::Node<iox2::ServiceType::Ipc>& node,
    const std::string& service_name,
    std::uint64_t buffer_size)
    -> iox2::Subscriber<iox2::ServiceType::Ipc,
                        iox2::bb::Slice<handpose_det::HandPoseDetection>,
                        handpose_det::HandPoseFrameMetadata>
  {
    auto service_result = node.service_builder(service_name_from_string(service_name))
      .publish_subscribe<iox2::bb::Slice<handpose_det::HandPoseDetection>>()
      .user_header<handpose_det::HandPoseFrameMetadata>()
      .open_or_create();

    if (!service_result.has_value()) {
      throw std::runtime_error("Failed to open handpose service: " + service_name);
    }

    auto subscriber_result = service_result.value()
      .subscriber_builder()
      .buffer_size(buffer_size)
      .create();

    if (!subscriber_result.has_value()) {
      throw std::runtime_error("Failed to create handpose subscriber");
    }

    return std::move(subscriber_result.value());
  }

  IpcHandposeSubscriber::IpcHandposeSubscriber(
    const std::string& service_name,
    std::uint64_t subscriber_buffer_size)
    : node_(create_node()),
      subscriber_(create_subscriber(node_, service_name, subscriber_buffer_size)) {}

  auto IpcSignlangPublisher::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    auto node_result = iox2::NodeBuilder()
      .create<iox2::ServiceType::Ipc>();
    if (!node_result.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 node for signlang publisher");
    }
    return std::move(node_result.value());
  }

  auto IpcSignlangPublisher::create_publisher(
    const iox2::Node<iox2::ServiceType::Ipc>& node,
    const std::string& service_name)
    -> iox2::Publisher<iox2::ServiceType::Ipc, SignlangResult, void>
  {
    auto service_result = node.service_builder(service_name_from_string(SignlangResult::IOX2_TYPE_NAME))
      .publish_subscribe<SignlangResult>()
      .open_or_create();

    if (!service_result.has_value()) {
      throw std::runtime_error("Failed to open signlang result service: " + service_name);
    }

    auto publisher_result = service_result.value()
      .publisher_builder()
      .create();

    if (!publisher_result.has_value()) {
      throw std::runtime_error("Failed to create signlang result publisher");
    }

    return std::move(publisher_result.value());
  }

  IpcSignlangPublisher::IpcSignlangPublisher(const std::string& service_name)
    : node_(create_node()),
      publisher_(create_publisher(node_, service_name)) {
    static_cast<void>(service_name);
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

} // namespace signlang::signlang_det
