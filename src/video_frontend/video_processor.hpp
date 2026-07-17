#ifndef SIGNLANG_EYES_VIDEO_FRONTEND_VIDEO_PROCESSOR_HPP
#define SIGNLANG_EYES_VIDEO_FRONTEND_VIDEO_PROCESSOR_HPP

#include "v4l2_capture_device.hpp"
#include "video_format.hpp"

#include "iox2/bb/slice.hpp"

#include <memory>

namespace signlang::video_frontend {

  class VideoProcessor {
  public:
    VideoProcessor(VideoFormat capture_format, VideoFormat output_format, bool mirror_output,
                   std::uint32_t rotation_degrees);
    ~VideoProcessor();

    VideoProcessor(const VideoProcessor&) = delete;
    auto operator=(const VideoProcessor&) -> VideoProcessor& = delete;
    VideoProcessor(VideoProcessor&&) = delete;
    auto operator=(VideoProcessor&&) -> VideoProcessor& = delete;

    [[nodiscard]] auto capture_format() const -> VideoFormat;
    [[nodiscard]] auto output_format() const -> VideoFormat;
    [[nodiscard]] auto max_output_size_bytes(std::uint32_t capture_max_frame_size_bytes) const -> std::uint32_t;
    [[nodiscard]] auto output_size_bytes(const CapturedVideoFrame& captured_frame) const -> std::uint32_t;
    void process(const CapturedVideoFrame& captured_frame, iox2::bb::MutableSlice<std::uint8_t> output_payload) const;

  private:
    class MjpegDecoder;

    [[nodiscard]] auto rgb_output_size_bytes() const -> std::uint32_t;
    void yuyv_to_resized_rgb(const CapturedVideoFrame& captured_frame,
                             iox2::bb::MutableSlice<std::uint8_t> output_payload) const;
    void mjpeg_to_resized_rgb(const CapturedVideoFrame& captured_frame,
                              iox2::bb::MutableSlice<std::uint8_t> output_payload) const;

    VideoFormat capture_format_;
    VideoFormat output_format_;
    bool mirror_output_;
    std::uint32_t rotation_degrees_;
    std::unique_ptr<MjpegDecoder> mjpeg_decoder_;
  };

} // namespace signlang::video_frontend

#endif // SIGNLANG_EYES_VIDEO_FRONTEND_VIDEO_PROCESSOR_HPP
