#include "video_processor.hpp"

#include "video_frame.hpp"

#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace signlang::video_frontend {
  namespace {

    constexpr auto kYuyvBytesPerPixel = std::uint32_t{2};

    auto checked_yuyv_size_bytes(VideoFormat format) -> std::uint32_t {
      const auto size_bytes = static_cast<std::uint64_t>(format.width) * format.height * kYuyvBytesPerPixel;
      if (size_bytes > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("YUYV video frame exceeds supported payload size");
      }

      return static_cast<std::uint32_t>(size_bytes);
    }

    auto is_even(std::uint32_t value) -> bool { return (value % 2) == 0; }

  } // namespace

  VideoProcessor::VideoProcessor(VideoFormat capture_format, VideoFormat output_format) :
      capture_format_{capture_format}, output_format_{output_format} {
    if (capture_format_.pixel_format != output_format_.pixel_format) {
      throw std::runtime_error("Video output pixel format must match capture pixel format");
    }

    if (needs_resize() && capture_format_.pixel_format != kPixelFormatYuyv) {
      throw std::runtime_error("Output resolution scaling is only supported for YUYV capture");
    }

    if (needs_resize()) {
      if (!is_even(capture_format_.width) || !is_even(output_format_.width)) {
        throw std::runtime_error("YUYV scaling requires even capture and output widths");
      }

      initialize_yuyv_resize_maps();
    }
  }

  auto VideoProcessor::capture_format() const -> VideoFormat { return capture_format_; }

  auto VideoProcessor::output_format() const -> VideoFormat { return output_format_; }

  auto VideoProcessor::max_output_size_bytes(std::uint32_t capture_max_frame_size_bytes) const -> std::uint32_t {
    if (!needs_resize()) {
      return capture_max_frame_size_bytes;
    }

    return yuyv_output_size_bytes();
  }

  auto VideoProcessor::output_size_bytes(const CapturedVideoFrame& captured_frame) const -> std::uint32_t {
    if (!needs_resize()) {
      return captured_frame.size_bytes;
    }

    return yuyv_output_size_bytes();
  }

  void VideoProcessor::process(const CapturedVideoFrame& captured_frame,
                               iox2::bb::MutableSlice<std::uint8_t> output_payload) const {
    if (!needs_resize()) {
      copy_frame(captured_frame, output_payload);
      return;
    }

    resize_yuyv(captured_frame, output_payload);
  }

  auto VideoProcessor::needs_resize() const -> bool {
    return capture_format_.width != output_format_.width || capture_format_.height != output_format_.height;
  }

  auto VideoProcessor::yuyv_output_size_bytes() const -> std::uint32_t { return checked_yuyv_size_bytes(output_format_); }

  auto VideoProcessor::yuyv_capture_size_bytes() const -> std::uint32_t {
    return checked_yuyv_size_bytes(capture_format_);
  }

  void VideoProcessor::initialize_yuyv_resize_maps() {
    yuyv_source_row_offsets_.resize(output_format_.height);
    for (std::uint32_t output_y = 0; output_y < output_format_.height; ++output_y) {
      const auto source_y =
          static_cast<std::uint32_t>((static_cast<std::uint64_t>(output_y) * capture_format_.height) /
                                     output_format_.height);
      yuyv_source_row_offsets_[output_y] = source_y * capture_format_.width * kYuyvBytesPerPixel;
    }

    yuyv_pair_mappings_.resize(output_format_.width / 2);
    for (std::uint32_t output_x = 0; output_x < output_format_.width; output_x += 2) {
      const auto source_x0 = static_cast<std::uint32_t>((static_cast<std::uint64_t>(output_x) * capture_format_.width) /
                                                        output_format_.width);
      const auto source_x1 =
          static_cast<std::uint32_t>((static_cast<std::uint64_t>(output_x + 1) * capture_format_.width) /
                                     output_format_.width);
      const auto source_pair_x = source_x0 / 2;
      const auto mapping_index = output_x / 2;

      yuyv_pair_mappings_[mapping_index] = YuyvPairMapping{
          .first_luma_offset = (source_pair_x * 4) + ((source_x0 % 2) * 2),
          .second_luma_offset = ((source_x1 / 2) * 4) + ((source_x1 % 2) * 2),
          .chroma_offset = (source_pair_x * 4) + 1,
      };
    }
  }

  void VideoProcessor::copy_frame(const CapturedVideoFrame& captured_frame,
                                  iox2::bb::MutableSlice<std::uint8_t> output_payload) const {
    if (captured_frame.size_bytes > output_payload.number_of_elements()) {
      throw std::runtime_error("Captured video frame exceeds loaned output payload size");
    }

    std::memcpy(output_payload.data(), captured_frame.data, captured_frame.size_bytes);
  }

  void VideoProcessor::resize_yuyv(const CapturedVideoFrame& captured_frame,
                                   iox2::bb::MutableSlice<std::uint8_t> output_payload) const {
    const auto required_input_size = yuyv_capture_size_bytes();
    if (captured_frame.size_bytes < required_input_size) {
      throw std::runtime_error("Captured YUYV frame is smaller than expected");
    }

    const auto required_output_size = yuyv_output_size_bytes();
    if (required_output_size > output_payload.number_of_elements()) {
      throw std::runtime_error("Resized video frame exceeds loaned output payload size");
    }

    const auto* input_data = captured_frame.data;
    auto* output_data = output_payload.data();
    const auto output_stride_bytes = output_format_.width * kYuyvBytesPerPixel;

    for (std::uint32_t output_y = 0; output_y < output_format_.height; ++output_y) {
      const auto* source_row = input_data + yuyv_source_row_offsets_[output_y];
      auto* output_row = output_data + (static_cast<std::uint64_t>(output_y) * output_stride_bytes);

      for (std::uint32_t mapping_index = 0; mapping_index < yuyv_pair_mappings_.size(); ++mapping_index) {
        const auto& mapping = yuyv_pair_mappings_[mapping_index];
        auto* output_pair = output_row + (mapping_index * 4);

        output_pair[0] = source_row[mapping.first_luma_offset];
        output_pair[1] = source_row[mapping.chroma_offset];
        output_pair[2] = source_row[mapping.second_luma_offset];
        output_pair[3] = source_row[mapping.chroma_offset + 2];
      }
    }
  }

} // namespace signlang::video_frontend
