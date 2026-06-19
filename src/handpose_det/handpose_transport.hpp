#ifndef SIGNLANG_EYES_HANDPOSE_DET_HANDPOSE_TRANSPORT_HPP
#define SIGNLANG_EYES_HANDPOSE_DET_HANDPOSE_TRANSPORT_HPP

#include "handpose_frame.hpp"
#include "video_frontend/video_frame.hpp"

#include "iox2/iceoryx2.hpp"
#include "state_machine/app_state.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace signlang::handpose_det {

  struct VideoSampleView {
    const signlang::video_frontend::VideoFrameMetadata* metadata;
    const std::uint8_t* payload;
    std::uint64_t payload_size;
  };

  struct HandPosePublishInfo {
    std::uint32_t detection_count;
    std::uint32_t image_width;
    std::uint32_t image_height;
    std::uint32_t model_width;
    std::uint32_t model_height;
  };

  class HandPoseTransport {
  public:
    HandPoseTransport(const std::string& input_service_name, const std::string& output_service_name,
                      std::uint64_t subscriber_buffer_size, std::uint32_t max_detections);

    HandPoseTransport(const HandPoseTransport&) = delete;
    auto operator=(const HandPoseTransport&) -> HandPoseTransport& = delete;
    HandPoseTransport(HandPoseTransport&&) = delete;
    auto operator=(HandPoseTransport&&) -> HandPoseTransport& = delete;

    template <typename Handler>
    auto receive_latest(Handler&& handler) -> bool;

    void publish(const signlang::video_frontend::VideoFrameMetadata& source_metadata, std::uint64_t sequence_number,
                 HandPosePublishInfo publish_info, const HandPoseDetection* detections);

    auto wait_for_work() -> bool;

  private:
    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_video_subscriber(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name,
                                        std::uint64_t buffer_size)
        -> iox2::Subscriber<iox2::ServiceType::Ipc, iox2::bb::Slice<std::uint8_t>,
                            signlang::video_frontend::VideoFrameMetadata>;
    static auto create_handpose_publisher(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                          const std::string& service_name, std::uint32_t max_detections)
        -> iox2::Publisher<iox2::ServiceType::Ipc, iox2::bb::Slice<HandPoseDetection>, HandPoseFrameMetadata>;
    static auto timestamp_ns() -> std::uint64_t;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    iox2::Subscriber<iox2::ServiceType::Ipc, iox2::bb::Slice<std::uint8_t>,
                     signlang::video_frontend::VideoFrameMetadata>
        subscriber_;
    iox2::Publisher<iox2::ServiceType::Ipc, iox2::bb::Slice<HandPoseDetection>, HandPoseFrameMetadata> publisher_;
    std::uint32_t max_detections_;
  };

  class IpcHandPoseStateMonitor {
  public:
    IpcHandPoseStateMonitor(const std::string& event_service_name, const std::string& blackboard_service_name);

    IpcHandPoseStateMonitor(const IpcHandPoseStateMonitor&) = delete;
    auto operator=(const IpcHandPoseStateMonitor&) -> IpcHandPoseStateMonitor& = delete;
    IpcHandPoseStateMonitor(IpcHandPoseStateMonitor&&) = delete;
    auto operator=(IpcHandPoseStateMonitor&&) -> IpcHandPoseStateMonitor& = delete;

    auto is_enabled() const -> bool;
    void wait_for_state_change_blocking();
    auto try_wait_for_state_change() -> bool;

  private:
    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_listener(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> iox2::Listener<iox2::ServiceType::Ipc>;
    static auto open_blackboard_service(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> iox2::PortFactoryBlackboard<iox2::ServiceType::Ipc, signlang::state_machine::AppStateKey>;
    static auto create_reader(
        const iox2::PortFactoryBlackboard<iox2::ServiceType::Ipc, signlang::state_machine::AppStateKey>& service)
        -> iox2::Reader<iox2::ServiceType::Ipc, signlang::state_machine::AppStateKey>;

    auto read_state_from_blackboard() -> signlang::state_machine::AppState;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    iox2::Listener<iox2::ServiceType::Ipc> listener_;
    iox2::PortFactoryBlackboard<iox2::ServiceType::Ipc, signlang::state_machine::AppStateKey> blackboard_service_;
    iox2::Reader<iox2::ServiceType::Ipc, signlang::state_machine::AppStateKey> reader_;
    signlang::state_machine::AppState cached_state_;
  };

  template <typename Handler>
  auto HandPoseTransport::receive_latest(Handler&& handler) -> bool {
    auto latest_sample = subscriber_.receive();
    if (!latest_sample.has_value()) {
      throw std::runtime_error("Failed to receive video sample through iceoryx2");
    }

    if (!latest_sample.value().has_value()) {
      return false;
    }

    while (true) {
      auto next_sample = subscriber_.receive();
      if (!next_sample.has_value()) {
        throw std::runtime_error("Failed to receive video sample through iceoryx2");
      }
      if (!next_sample.value().has_value()) {
        break;
      }
      latest_sample = std::move(next_sample);
    }

    const auto& current_sample = latest_sample.value().value();
    const auto payload = current_sample.payload();
    const auto& metadata = current_sample.user_header();
    handler(VideoSampleView{
        .metadata = &metadata,
        .payload = payload.data(),
        .payload_size = payload.number_of_elements(),
    });

    return true;
  }

  inline void HandPoseTransport::publish(const signlang::video_frontend::VideoFrameMetadata& source_metadata,
                                         std::uint64_t sequence_number, HandPosePublishInfo publish_info,
                                         const HandPoseDetection* detections) {
    if (publish_info.detection_count > max_detections_) {
      throw std::runtime_error("Hand pose detection count exceeds output payload capacity");
    }

    const auto loan_count = std::max<std::uint32_t>(1, publish_info.detection_count);
    auto loan_result = publisher_.loan_slice_uninit(loan_count);
    if (!loan_result.has_value()) {
      throw std::runtime_error("Failed to loan iceoryx2 handpose sample");
    }

    auto loaned_sample = std::move(loan_result.value());
    auto payload = loaned_sample.payload_mut();
    for (std::uint32_t i = 0; i < publish_info.detection_count; ++i) {
      payload[i] = detections[i];
    }
    if (publish_info.detection_count == 0) {
      payload[0] = HandPoseDetection{};
    }

    auto& metadata = loaned_sample.user_header_mut();
    metadata.sequence_number = sequence_number;
    metadata.timestamp_ns = timestamp_ns();
    metadata.source_sequence_number = source_metadata.sequence_number;
    metadata.source_timestamp_ns = source_metadata.timestamp_ns;
    metadata.image_width = publish_info.image_width;
    metadata.image_height = publish_info.image_height;
    metadata.model_width = publish_info.model_width;
    metadata.model_height = publish_info.model_height;
    metadata.source_pixel_format = source_metadata.pixel_format;
    metadata.detection_count = publish_info.detection_count;
    metadata.keypoint_count = kHandPoseKeypointCount;
    metadata.payload_count = publish_info.detection_count;

    auto initialized_sample = iox2::assume_init(std::move(loaned_sample));
    const auto send_result = iox2::send(std::move(initialized_sample));
    if (!send_result.has_value()) {
      throw std::runtime_error("Failed to publish handpose detections through iceoryx2");
    }
  }

} // namespace signlang::handpose_det

#endif // SIGNLANG_EYES_HANDPOSE_DET_HANDPOSE_TRANSPORT_HPP
