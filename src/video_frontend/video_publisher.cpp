#include "video_publisher.hpp"

#include "common/ipc_utils.hpp"
#include "common/time.hpp"
#include "v4l2_capture_device.hpp"
#include "video_processor.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace signlang::video_frontend {
  VideoPublisher::VideoPublisher(const std::string& service_name, std::uint32_t max_payload_size_bytes) :
      node_{create_node()}, service_{create_service(node_, service_name)},
      publisher_{create_publisher(service_, max_payload_size_bytes)}, max_payload_size_bytes_{max_payload_size_bytes} {}

  auto VideoPublisher::has_subscribers() const -> bool { return signlang::common::ipc::has_subscribers(service_); }

  void VideoPublisher::publish(const CapturedVideoFrame& captured_frame, const VideoProcessor& video_processor,
                               std::uint32_t fps, std::uint64_t sequence_number) {
    const auto output_size_bytes = video_processor.output_size_bytes(captured_frame);
    if (output_size_bytes > max_payload_size_bytes_) {
      throw std::runtime_error("Processed video frame exceeds iceoryx2 payload capacity");
    }

    auto loan_result = publisher_.loan_slice_uninit(output_size_bytes);
    if (!loan_result.has_value()) {
      throw std::runtime_error("Failed to loan iceoryx2 video frame sample");
    }

    auto loaned_sample = std::move(loan_result.value());
    const auto capture_format = video_processor.capture_format();
    const auto output_format = video_processor.output_format();
    auto& metadata = loaned_sample.user_header_mut();
    metadata.sequence_number = sequence_number;
    metadata.timestamp_ns = common::steady_timestamp_ns();
    metadata.capture_width = capture_format.width;
    metadata.capture_height = capture_format.height;
    metadata.output_width = output_format.width;
    metadata.output_height = output_format.height;
    metadata.fps = fps;
    metadata.pixel_format = output_format.pixel_format;
    metadata.payload_size_bytes = output_size_bytes;

    auto payload = loaned_sample.payload_mut();
    video_processor.process(captured_frame, payload);

    auto initialized_sample = iox2::assume_init(std::move(loaned_sample));
    const auto send_result = iox2::send(std::move(initialized_sample));
    if (!send_result.has_value()) {
      throw std::runtime_error("Failed to publish video frame through iceoryx2");
    }
  }

  auto VideoPublisher::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    iox2::set_log_level_from_env_or(iox2::LogLevel::Warn);

    auto node =
        iox2::NodeBuilder().signal_handling_mode(iox2::SignalHandlingMode::Disabled).create<iox2::ServiceType::Ipc>();
    if (!node.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 IPC node");
    }

    return std::move(node.value());
  }

  auto VideoPublisher::create_service(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
      -> VideoService {
    auto service = node.service_builder(signlang::common::ipc::service_name_from_string(service_name))
                       .publish_subscribe<iox2::bb::Slice<std::uint8_t>>()
                       .user_header<VideoFrameMetadata>()
                       .open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open or create iceoryx2 service: " + service_name);
    }
    return std::move(service.value());
  }

  auto VideoPublisher::create_publisher(const VideoService& service, std::uint32_t max_payload_size_bytes)
      -> iox2::Publisher<iox2::ServiceType::Ipc, iox2::bb::Slice<std::uint8_t>, VideoFrameMetadata> {
    auto publisher = service.publisher_builder()
                         .initial_max_slice_len(max_payload_size_bytes)
                         .allocation_strategy(iox2::AllocationStrategy::Static)
                         .create();
    if (!publisher.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 video publisher");
    }

    return std::move(publisher.value());
  }

} // namespace signlang::video_frontend
