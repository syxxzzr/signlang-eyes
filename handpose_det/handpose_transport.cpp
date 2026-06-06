#include "handpose_transport.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace signlang::handpose_det {
  namespace {

    constexpr auto kWaitPeriodMs = std::uint64_t{5};

  } // namespace

  HandPoseTransport::HandPoseTransport(const std::string& input_service_name, const std::string& output_service_name,
                                       std::uint64_t subscriber_buffer_size, std::uint32_t max_detections) :
      node_{create_node()},
      subscriber_{create_video_subscriber(node_, input_service_name, subscriber_buffer_size)},
      publisher_{create_handpose_publisher(node_, output_service_name, max_detections)}, max_detections_{max_detections} {}

  auto HandPoseTransport::wait_for_work() -> bool {
    return node_.wait(iox2::bb::Duration::from_millis(kWaitPeriodMs)).has_value();
  }

  auto HandPoseTransport::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    iox2::set_log_level_from_env_or(iox2::LogLevel::Warn);
    auto node = iox2::NodeBuilder().create<iox2::ServiceType::Ipc>();
    if (!node.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 IPC node");
    }

    return std::move(node.value());
  }

  auto HandPoseTransport::create_video_subscriber(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                                  const std::string& service_name, std::uint64_t buffer_size)
      -> iox2::Subscriber<iox2::ServiceType::Ipc, iox2::bb::Slice<std::uint8_t>,
                          signlang::video_frontend::VideoFrameMetadata> {
    const auto parsed_service_name = iox2::ServiceName::create(service_name.c_str());
    if (!parsed_service_name.has_value()) {
      throw std::runtime_error("Invalid upstream iceoryx2 service name: " + service_name);
    }

    auto service = node.service_builder(parsed_service_name.value())
                       .publish_subscribe<iox2::bb::Slice<std::uint8_t>>()
                       .user_header<signlang::video_frontend::VideoFrameMetadata>()
                       .open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open upstream iceoryx2 service: " + service_name);
    }

    auto subscriber = service.value().subscriber_builder().buffer_size(buffer_size).create();
    if (!subscriber.has_value()) {
      throw std::runtime_error("Failed to create upstream video subscriber: " + service_name);
    }

    return std::move(subscriber.value());
  }

  auto HandPoseTransport::create_handpose_publisher(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                                    const std::string& service_name, std::uint32_t max_detections)
      -> iox2::Publisher<iox2::ServiceType::Ipc, iox2::bb::Slice<HandPoseDetection>, HandPoseFrameMetadata> {
    const auto parsed_service_name = iox2::ServiceName::create(service_name.c_str());
    if (!parsed_service_name.has_value()) {
      throw std::runtime_error("Invalid handpose iceoryx2 service name: " + service_name);
    }

    auto service = node.service_builder(parsed_service_name.value())
                       .publish_subscribe<iox2::bb::Slice<HandPoseDetection>>()
                       .user_header<HandPoseFrameMetadata>()
                       .open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open or create handpose iceoryx2 service: " + service_name);
    }

    auto publisher = service.value()
                         .publisher_builder()
                         .initial_max_slice_len(std::max<std::uint32_t>(1, max_detections))
                         .allocation_strategy(iox2::AllocationStrategy::Static)
                         .create();
    if (!publisher.has_value()) {
      throw std::runtime_error("Failed to create handpose publisher: " + service_name);
    }

    return std::move(publisher.value());
  }

  auto HandPoseTransport::timestamp_ns() -> std::uint64_t {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
  }

} // namespace signlang::handpose_det
