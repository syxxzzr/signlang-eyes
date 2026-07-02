#include "handpose_transport.hpp"

#include "common/ipc_utils.hpp"
#include "common/time.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace signlang::handpose_det {
  namespace {

    constexpr auto kWaitPeriodMs = std::uint64_t{5};

  } // namespace

  HandPoseTransport::HandPoseTransport(std::string input_service_name, const std::string& output_service_name,
                                       std::uint64_t subscriber_buffer_size, std::uint32_t hand_slots) :
      node_{create_node()}, input_service_name_{std::move(input_service_name)},
      subscriber_buffer_size_{subscriber_buffer_size}, service_{create_handpose_service(node_, output_service_name)},
      publisher_{create_handpose_publisher(service_, hand_slots)}, hand_slots_{hand_slots} {}

  auto HandPoseTransport::wait_for_work() -> bool {
    if (!subscriber_.has_value()) {
      return false;
    }
    return node_.wait(iox2::bb::Duration::from_millis(kWaitPeriodMs)).has_value();
  }

  auto HandPoseTransport::has_subscribers() const -> bool { return signlang::common::ipc::has_subscribers(service_); }

  void HandPoseTransport::detach_upstream() { subscriber_.reset(); }

  void HandPoseTransport::ensure_upstream_attached() {
    if (!subscriber_.has_value()) {
      subscriber_.emplace(create_video_subscriber(node_, input_service_name_, subscriber_buffer_size_));
    }
  }

  auto HandPoseTransport::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    iox2::set_log_level_from_env_or(iox2::LogLevel::Warn);
    auto node =
        iox2::NodeBuilder().signal_handling_mode(iox2::SignalHandlingMode::Disabled).create<iox2::ServiceType::Ipc>();
    if (!node.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 IPC node");
    }

    return std::move(node.value());
  }

  auto HandPoseTransport::create_video_subscriber(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                                  const std::string& service_name, std::uint64_t buffer_size)
      -> iox2::Subscriber<iox2::ServiceType::Ipc, iox2::bb::Slice<std::uint8_t>,
                          signlang::video_frontend::VideoFrameMetadata> {
    auto service = node.service_builder(signlang::common::ipc::service_name_from_string(service_name))
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

  auto HandPoseTransport::create_handpose_service(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                                  const std::string& service_name) -> HandposeService {
    auto service = node.service_builder(signlang::common::ipc::service_name_from_string(service_name))
                       .publish_subscribe<iox2::bb::Slice<HandPoseDetection>>()
                       .user_header<HandPoseFrameMetadata>()
                       .open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open or create handpose iceoryx2 service: " + service_name);
    }

    return std::move(service.value());
  }

  auto HandPoseTransport::create_handpose_publisher(const HandposeService& service, std::uint32_t hand_slots)
      -> iox2::Publisher<iox2::ServiceType::Ipc, iox2::bb::Slice<HandPoseDetection>, HandPoseFrameMetadata> {
    auto publisher = service.publisher_builder()
                         .initial_max_slice_len(hand_slots)
                         .allocation_strategy(iox2::AllocationStrategy::Static)
                         .create();
    if (!publisher.has_value()) {
      throw std::runtime_error("Failed to create handpose publisher");
    }

    return std::move(publisher.value());
  }

  auto HandPoseTransport::timestamp_ns() -> std::uint64_t { return common::steady_timestamp_ns(); }

} // namespace signlang::handpose_det
