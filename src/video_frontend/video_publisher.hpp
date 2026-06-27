#ifndef SIGNLANG_EYES_VIDEO_FRONTEND_VIDEO_PUBLISHER_HPP
#define SIGNLANG_EYES_VIDEO_FRONTEND_VIDEO_PUBLISHER_HPP

#include "video_format.hpp"
#include "video_frame.hpp"

#include "iox2/iceoryx2.hpp"

#include <cstdint>
#include <string>

namespace signlang::video_frontend {

  struct CapturedVideoFrame;
  class VideoProcessor;

  class VideoPublisher {
  public:
    VideoPublisher(const std::string& service_name, std::uint32_t max_payload_size_bytes);

    VideoPublisher(const VideoPublisher&) = delete;
    auto operator=(const VideoPublisher&) -> VideoPublisher& = delete;
    VideoPublisher(VideoPublisher&&) = delete;
    auto operator=(VideoPublisher&&) -> VideoPublisher& = delete;

    void publish(const CapturedVideoFrame& captured_frame, const VideoProcessor& video_processor, std::uint32_t fps,
                 std::uint64_t sequence_number);
    auto has_subscribers() const -> bool;

  private:
    using VideoService =
        iox2::PortFactoryPublishSubscribe<iox2::ServiceType::Ipc, iox2::bb::Slice<std::uint8_t>, VideoFrameMetadata>;

    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_service(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> VideoService;
    static auto create_publisher(const VideoService& service, std::uint32_t max_payload_size_bytes)
        -> iox2::Publisher<iox2::ServiceType::Ipc, iox2::bb::Slice<std::uint8_t>, VideoFrameMetadata>;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    VideoService service_;
    iox2::Publisher<iox2::ServiceType::Ipc, iox2::bb::Slice<std::uint8_t>, VideoFrameMetadata> publisher_;
    std::uint32_t max_payload_size_bytes_;
  };

} // namespace signlang::video_frontend

#endif // SIGNLANG_EYES_VIDEO_FRONTEND_VIDEO_PUBLISHER_HPP
