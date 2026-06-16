#ifndef SIGNLANG_EYES_EDGEAI_VIDEO_FRONTEND_V4L2_CAPTURE_DEVICE_HPP
#define SIGNLANG_EYES_EDGEAI_VIDEO_FRONTEND_V4L2_CAPTURE_DEVICE_HPP

#include "video_format.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace signlang::video_frontend {

  struct CapturedVideoFrame {
    const std::uint8_t* data;
    std::uint32_t size_bytes;
  };

  class V4l2CaptureDevice {
  public:
    V4l2CaptureDevice(const std::string& device_name, VideoFormatRequest format_request, std::uint32_t fps);
    ~V4l2CaptureDevice();

    V4l2CaptureDevice(const V4l2CaptureDevice&) = delete;
    auto operator=(const V4l2CaptureDevice&) -> V4l2CaptureDevice& = delete;
    V4l2CaptureDevice(V4l2CaptureDevice&&) = delete;
    auto operator=(V4l2CaptureDevice&&) -> V4l2CaptureDevice& = delete;

    auto format() const -> VideoFormat;
    auto fps() const -> std::uint32_t;
    auto max_frame_size_bytes() const -> std::uint32_t;
    auto capture_frame() -> CapturedVideoFrame;
    void release_frame();

  private:
    struct MappedBuffer {
      void* start;
      std::size_t length;
    };

    void open_device();
    void configure();
    void select_format();
    auto select_largest_frame_size(std::uint32_t pixel_format) const -> VideoFormat;
    auto supports_frame_size(std::uint32_t pixel_format, std::uint32_t width, std::uint32_t height) const -> bool;
    void configure_format();
    void configure_fps();
    void configure_buffers();
    void start_streaming();
    void stop_streaming() noexcept;
    void close_device() noexcept;
    void unmap_buffers() noexcept;
    void enqueue_buffer(std::uint32_t buffer_index);
    auto dequeue_frame() -> CapturedVideoFrame;

    std::string device_name_;
    VideoFormatRequest format_request_;
    std::uint32_t requested_fps_;
    int device_fd_;
    VideoFormat format_;
    std::uint32_t fps_;
    std::uint32_t max_frame_size_bytes_;
    std::vector<MappedBuffer> mapped_buffers_;
    std::int32_t active_buffer_index_;
    bool streaming_;
  };

} // namespace signlang::video_frontend

#endif // SIGNLANG_EYES_EDGEAI_VIDEO_FRONTEND_V4L2_CAPTURE_DEVICE_HPP
