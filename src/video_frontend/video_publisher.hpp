#ifndef SIGNLANG_EYES_EDGEAI_VIDEO_FRONTEND_VIDEO_PUBLISHER_HPP
#define SIGNLANG_EYES_EDGEAI_VIDEO_FRONTEND_VIDEO_PUBLISHER_HPP

#include "video_frame.hpp"
#include "video_format.hpp"

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

  private:
    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_publisher(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name,
                                 std::uint32_t max_payload_size_bytes)
        -> iox2::Publisher<iox2::ServiceType::Ipc, iox2::bb::Slice<std::uint8_t>, VideoFrameMetadata>;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    iox2::Publisher<iox2::ServiceType::Ipc, iox2::bb::Slice<std::uint8_t>, VideoFrameMetadata> publisher_;
    std::uint32_t max_payload_size_bytes_;
  };

} // namespace signlang::video_frontend

#endif // SIGNLANG_EYES_EDGEAI_VIDEO_FRONTEND_VIDEO_PUBLISHER_HPP
