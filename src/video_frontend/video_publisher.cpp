#include "video_publisher.hpp"

#include "v4l2_capture_device.hpp"
#include "video_processor.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace signlang::video_frontend {
  namespace {

    auto steady_timestamp_ns() -> std::uint64_t {
      const auto now = std::chrono::steady_clock::now().time_since_epoch();
      return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
    }

  } // namespace

  VideoPublisher::VideoPublisher(const std::string& service_name, std::uint32_t max_payload_size_bytes) :
      node_{create_node()}, publisher_{create_publisher(node_, service_name, max_payload_size_bytes)},
      max_payload_size_bytes_{max_payload_size_bytes} {}

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
    metadata.timestamp_ns = steady_timestamp_ns();
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

    auto node = iox2::NodeBuilder().create<iox2::ServiceType::Ipc>();
    if (!node.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 IPC node");
    }

    return std::move(node.value());
  }

  auto VideoPublisher::create_publisher(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name,
                                        std::uint32_t max_payload_size_bytes)
      -> iox2::Publisher<iox2::ServiceType::Ipc, iox2::bb::Slice<std::uint8_t>, VideoFrameMetadata> {
    const auto parsed_service_name = iox2::ServiceName::create(service_name.c_str());
    if (!parsed_service_name.has_value()) {
      throw std::runtime_error("Invalid iceoryx2 service name: " + service_name);
    }

    auto service = node.service_builder(parsed_service_name.value())
                       .publish_subscribe<iox2::bb::Slice<std::uint8_t>>()
                       .user_header<VideoFrameMetadata>()
                       .open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open or create iceoryx2 service: " + service_name);
    }

    auto publisher = service.value()
                         .publisher_builder()
                         .initial_max_slice_len(max_payload_size_bytes)
                         .allocation_strategy(iox2::AllocationStrategy::Static)
                         .create();
    if (!publisher.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 publisher for service: " + service_name);
    }

    return std::move(publisher.value());
  }

} // namespace signlang::video_frontend
