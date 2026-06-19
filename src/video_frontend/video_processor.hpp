#ifndef SIGNLANG_EYES_VIDEO_FRONTEND_VIDEO_PROCESSOR_HPP
#define SIGNLANG_EYES_VIDEO_FRONTEND_VIDEO_PROCESSOR_HPP

#include "v4l2_capture_device.hpp"
#include "video_format.hpp"

#include "iox2/bb/slice.hpp"

#include <cstdint>
#include <vector>

namespace signlang::video_frontend {

  class VideoProcessor {
  public:
    VideoProcessor(VideoFormat capture_format, VideoFormat output_format);

    auto capture_format() const -> VideoFormat;
    auto output_format() const -> VideoFormat;
    auto max_output_size_bytes(std::uint32_t capture_max_frame_size_bytes) const -> std::uint32_t;
    auto output_size_bytes(const CapturedVideoFrame& captured_frame) const -> std::uint32_t;
    void process(const CapturedVideoFrame& captured_frame, iox2::bb::MutableSlice<std::uint8_t> output_payload) const;

  private:
    struct YuyvPairMapping {
      std::uint32_t first_luma_offset;
      std::uint32_t second_luma_offset;
      std::uint32_t chroma_offset;
    };

    auto needs_resize() const -> bool;
    auto yuyv_output_size_bytes() const -> std::uint32_t;
    auto yuyv_capture_size_bytes() const -> std::uint32_t;
    void initialize_yuyv_resize_maps();
    void copy_frame(const CapturedVideoFrame& captured_frame, iox2::bb::MutableSlice<std::uint8_t> output_payload) const;
    void resize_yuyv(const CapturedVideoFrame& captured_frame,
                     iox2::bb::MutableSlice<std::uint8_t> output_payload) const;

    VideoFormat capture_format_;
    VideoFormat output_format_;
    std::vector<std::uint32_t> yuyv_source_row_offsets_;
    std::vector<YuyvPairMapping> yuyv_pair_mappings_;
  };

} // namespace signlang::video_frontend

#endif // SIGNLANG_EYES_VIDEO_FRONTEND_VIDEO_PROCESSOR_HPP
