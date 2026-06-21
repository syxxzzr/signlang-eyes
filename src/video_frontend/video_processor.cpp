#include "video_processor.hpp"

#include "video_frame.hpp"

#include "im2d.h"
#include "rga.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace signlang::video_frontend {
  namespace {

    constexpr auto kYuyvBytesPerPixel = std::uint32_t{2};

    auto checked_size_bytes(VideoFormat format, std::uint32_t bytes_per_pixel, const char* label) -> std::uint32_t {
      const auto size_bytes = static_cast<std::uint64_t>(format.width) * format.height * bytes_per_pixel;
      if (size_bytes > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error(std::string{label} + " video frame exceeds supported payload size");
      }

      return static_cast<std::uint32_t>(size_bytes);
    }

    auto is_even(std::uint32_t value) -> bool { return (value % 2) == 0; }

  } // namespace

  VideoProcessor::VideoProcessor(VideoFormat capture_format, VideoFormat output_format) :
      capture_format_{capture_format}, output_format_{output_format}, jpeg_decompressor_{nullptr} {
    if (output_format_.pixel_format != kPixelFormatRgb24) {
      throw std::runtime_error("Video output pixel format must be RGB24");
    }

    if (capture_format_.pixel_format != kPixelFormatYuyv && capture_format_.pixel_format != kPixelFormatMjpeg) {
      throw std::runtime_error("Video capture pixel format must be YUYV or MJPEG");
    }

    if (capture_format_.pixel_format == kPixelFormatYuyv && !is_even(capture_format_.width)) {
      throw std::runtime_error("YUYV capture width must be even");
    }

    // MJPEG requires intermediate RGB buffer for JPEG decompression
    if (capture_format_.pixel_format == kPixelFormatMjpeg) {
      capture_rgb_buffer_.resize(rgb_capture_size_bytes());
      jpeg_decompressor_ = tjInitDecompress();
      if (jpeg_decompressor_ == nullptr) {
        throw std::runtime_error("Failed to initialize TurboJPEG decompressor");
      }
    }
  }

  VideoProcessor::~VideoProcessor() {
    if (jpeg_decompressor_ != nullptr) {
      static_cast<void>(tjDestroy(jpeg_decompressor_));
    }
  }

  auto VideoProcessor::capture_format() const -> VideoFormat { return capture_format_; }

  auto VideoProcessor::output_format() const -> VideoFormat { return output_format_; }

  auto VideoProcessor::max_output_size_bytes(std::uint32_t capture_max_frame_size_bytes) const -> std::uint32_t {
    (void)capture_max_frame_size_bytes;
    return rgb_output_size_bytes();
  }

  auto VideoProcessor::output_size_bytes(const CapturedVideoFrame& captured_frame) const -> std::uint32_t {
    (void)captured_frame;
    return rgb_output_size_bytes();
  }

  void VideoProcessor::process(const CapturedVideoFrame& captured_frame,
                               iox2::bb::MutableSlice<std::uint8_t> output_payload) const {
    if (capture_format_.pixel_format == kPixelFormatYuyv) {
      yuyv_to_resized_rgb(captured_frame, output_payload);
      return;
    }

    mjpeg_to_resized_rgb(captured_frame, output_payload);
  }

  auto VideoProcessor::rgb_output_size_bytes() const -> std::uint32_t {
    return checked_size_bytes(output_format_, kRgbBytesPerPixel, "RGB output");
  }

  auto VideoProcessor::rgb_capture_size_bytes() const -> std::uint32_t {
    return checked_size_bytes(capture_format_, kRgbBytesPerPixel, "RGB capture");
  }

  void VideoProcessor::yuyv_to_resized_rgb(const CapturedVideoFrame& captured_frame,
                                           iox2::bb::MutableSlice<std::uint8_t> output_payload) const {
    const auto required_input_size = capture_format_.width * capture_format_.height * kYuyvBytesPerPixel;
    if (captured_frame.size_bytes < required_input_size) {
      throw std::runtime_error("Captured YUYV frame is smaller than expected");
    }

    const auto required_output_size = rgb_output_size_bytes();
    if (required_output_size > output_payload.number_of_elements()) {
      throw std::runtime_error("RGB video frame exceeds loaned output payload size");
    }

    // RGA hardware accelerator: YUYV→RGB conversion + resize in one operation
    const auto src_buffer_size = static_cast<int>(captured_frame.size_bytes);
    const auto src_handle = importbuffer_virtualaddr(const_cast<std::uint8_t*>(captured_frame.data), src_buffer_size);
    if (src_handle == 0) {
      throw std::runtime_error("RGA: failed to import YUYV source buffer");
    }

    const auto dst_buffer_size = static_cast<int>(required_output_size);
    const auto dst_handle = importbuffer_virtualaddr(output_payload.data(), dst_buffer_size);
    if (dst_handle == 0) {
      releasebuffer_handle(src_handle);
      throw std::runtime_error("RGA: failed to import RGB destination buffer");
    }

    const auto src_img = wrapbuffer_handle(src_handle, static_cast<int>(capture_format_.width),
                                           static_cast<int>(capture_format_.height), RK_FORMAT_YUYV_422);
    const auto dst_img = wrapbuffer_handle(dst_handle, static_cast<int>(output_format_.width),
                                           static_cast<int>(output_format_.height), RK_FORMAT_RGB_888);

    const auto status = imresize(src_img, dst_img, 0.0, 0.0, INTER_LINEAR, 1);

    releasebuffer_handle(dst_handle);
    releasebuffer_handle(src_handle);

    if (status != IM_STATUS_SUCCESS) {
      throw std::runtime_error(std::string{"RGA YUYV resize failed with status: "} +
                               std::to_string(static_cast<int>(status)));
    }
  }

  void VideoProcessor::mjpeg_to_resized_rgb(const CapturedVideoFrame& captured_frame,
                                            iox2::bb::MutableSlice<std::uint8_t> output_payload) const {
    if (jpeg_decompressor_ == nullptr) {
      throw std::runtime_error("TurboJPEG decompressor is not initialized");
    }

    const auto required_output_size = rgb_output_size_bytes();
    if (required_output_size > output_payload.number_of_elements()) {
      throw std::runtime_error("RGB video frame exceeds loaned output payload size");
    }

    // Step 1: Decode MJPEG to RGB at capture resolution
    int jpeg_width = 0;
    int jpeg_height = 0;
    int jpeg_subsampling = 0;
    int jpeg_colorspace = 0;
    if (tjDecompressHeader3(jpeg_decompressor_, captured_frame.data, captured_frame.size_bytes, &jpeg_width,
                            &jpeg_height, &jpeg_subsampling, &jpeg_colorspace) != 0) {
      throw std::runtime_error(std::string{"Failed to read MJPEG header: "} + tjGetErrorStr2(jpeg_decompressor_));
    }

    if (jpeg_width != static_cast<int>(capture_format_.width) ||
        jpeg_height != static_cast<int>(capture_format_.height)) {
      throw std::runtime_error("MJPEG frame dimensions do not match negotiated capture format");
    }

    const auto pitch = static_cast<int>(capture_format_.width * kRgbBytesPerPixel);
    if (tjDecompress2(jpeg_decompressor_, captured_frame.data, captured_frame.size_bytes, capture_rgb_buffer_.data(),
                      jpeg_width, pitch, jpeg_height, TJPF_RGB, TJFLAG_FASTDCT) != 0) {
      throw std::runtime_error(std::string{"Failed to decode MJPEG frame: "} + tjGetErrorStr2(jpeg_decompressor_));
    }

    // Step 2: Resize using RGA hardware accelerator
    const auto src_buffer_size = static_cast<int>(capture_rgb_buffer_.size());
    const auto src_handle = importbuffer_virtualaddr(capture_rgb_buffer_.data(), src_buffer_size);
    if (src_handle == 0) {
      throw std::runtime_error("RGA: failed to import RGB source buffer");
    }

    const auto dst_buffer_size = static_cast<int>(required_output_size);
    const auto dst_handle = importbuffer_virtualaddr(output_payload.data(), dst_buffer_size);
    if (dst_handle == 0) {
      releasebuffer_handle(src_handle);
      throw std::runtime_error("RGA: failed to import RGB destination buffer");
    }

    const auto src_img = wrapbuffer_handle(src_handle, static_cast<int>(capture_format_.width),
                                           static_cast<int>(capture_format_.height), RK_FORMAT_RGB_888);
    const auto dst_img = wrapbuffer_handle(dst_handle, static_cast<int>(output_format_.width),
                                           static_cast<int>(output_format_.height), RK_FORMAT_RGB_888);

    const auto status = imresize(src_img, dst_img, 0.0, 0.0, INTER_LINEAR, 1);

    releasebuffer_handle(dst_handle);
    releasebuffer_handle(src_handle);

    if (status != IM_STATUS_SUCCESS) {
      throw std::runtime_error(std::string{"RGA RGB resize failed with status: "} +
                               std::to_string(static_cast<int>(status)));
    }
  }

} // namespace signlang::video_frontend
