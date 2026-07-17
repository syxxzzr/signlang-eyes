#include "video_processor.hpp"

#include "video_frame.hpp"

#include "im2d.h"
#include "rga.h"
#include "rk_mpi.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace signlang::video_frontend {
  namespace {

    constexpr auto kYuyvBytesPerPixel = std::uint32_t{2};
    constexpr auto kRgbBytesPerPixel = std::uint32_t{3};
    constexpr auto kMppOutputAlignment = std::uint32_t{16};
    constexpr auto kMppOutputCapacityBytesPerPixel = std::size_t{4};
    constexpr auto kMppDecodeTimeoutMs = RK_S64{1000};
    constexpr auto kMppBufferTag = "video_frontend_mjpeg";

    auto checked_rgb_size_bytes(VideoFormat format, const char* label) -> std::uint32_t {
      const auto size_bytes = static_cast<std::uint64_t>(format.width) * format.height * kRgbBytesPerPixel;
      if (size_bytes > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error(std::string{label} + " video frame exceeds supported payload size");
      }

      return static_cast<std::uint32_t>(size_bytes);
    }

    auto is_even(std::uint32_t value) -> bool { return (value % 2) == 0; }

    auto checked_align_up(std::uint32_t value, std::uint32_t alignment, const char* label) -> std::uint32_t {
      const auto adjustment = alignment - 1;
      if (value > std::numeric_limits<std::uint32_t>::max() - adjustment) {
        throw std::runtime_error(std::string{label} + " exceeds MPP alignment limits");
      }

      return (value + adjustment) & ~adjustment;
    }

    auto checked_output_buffer_size(std::uint32_t width, std::uint32_t height) -> std::size_t {
      const auto aligned_width = checked_align_up(width, kMppOutputAlignment, "MJPEG width");
      const auto aligned_height = checked_align_up(height, kMppOutputAlignment, "MJPEG height");
      const auto pixel_count = static_cast<std::size_t>(aligned_width) * aligned_height;
      if (pixel_count > std::numeric_limits<std::size_t>::max() / kMppOutputCapacityBytesPerPixel) {
        throw std::runtime_error("MJPEG output frame exceeds MPP buffer size limits");
      }

      return pixel_count * kMppOutputCapacityBytesPerPixel;
    }

    auto checked_int(std::uint32_t value, const char* label) -> int {
      if (value > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error(std::string{label} + " exceeds RGA integer limits");
      }

      return static_cast<int>(value);
    }

    auto checked_int(std::size_t value, const char* label) -> int {
      if (value > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error(std::string{label} + " exceeds RGA integer limits");
      }

      return static_cast<int>(value);
    }

    void check_mpp(MPP_RET result, const char* operation) {
      if (result != MPP_OK) {
        throw std::runtime_error(std::string{"MPP: "} + operation + " failed with status " +
                                 std::to_string(static_cast<int>(result)));
      }
    }

    class MppPacketHandle {
    public:
      MppPacketHandle() = default;

      ~MppPacketHandle() {
        if (packet_ != nullptr) {
          static_cast<void>(mpp_packet_deinit(&packet_));
        }
      }

      MppPacketHandle(const MppPacketHandle&) = delete;
      auto operator=(const MppPacketHandle&) -> MppPacketHandle& = delete;

      [[nodiscard]] auto address() -> MppPacket* { return &packet_; }
      [[nodiscard]] auto get() const -> MppPacket { return packet_; }

    private:
      MppPacket packet_{nullptr};
    };

    auto rga_rotation_usage(std::uint32_t rotation_degrees) -> int {
      switch (rotation_degrees) {
      case 0:
        return 0;
      case 90:
        return IM_HAL_TRANSFORM_ROT_90;
      case 180:
        return IM_HAL_TRANSFORM_ROT_180;
      case 270:
        return IM_HAL_TRANSFORM_ROT_270;
      default:
        throw std::runtime_error("Video rotation must be one of: 0, 90, 180, 270");
      }
    }

    auto rga_transform_usage(std::uint32_t rotation_degrees, bool mirror_output) -> int {
      return IM_SYNC | rga_rotation_usage(rotation_degrees) | (mirror_output ? IM_HAL_TRANSFORM_FLIP_H : 0);
    }

  } // namespace

  class VideoProcessor::MjpegDecoder {
  public:
    struct DecodedFrame {
      int dma_fd;
      std::size_t buffer_size_bytes;
      std::uint32_t width;
      std::uint32_t height;
      std::uint32_t horizontal_stride_pixels;
      std::uint32_t vertical_stride_pixels;
    };

    explicit MjpegDecoder(VideoFormat capture_format) :
        capture_format_{capture_format}, context_{nullptr}, api_{nullptr}, buffer_group_{nullptr},
        input_buffer_{nullptr}, output_buffer_{nullptr}, output_frame_{nullptr} {
      try {
        initialize();
      } catch (...) {
        release();
        throw;
      }
    }

    ~MjpegDecoder() { release(); }

    MjpegDecoder(const MjpegDecoder&) = delete;
    auto operator=(const MjpegDecoder&) -> MjpegDecoder& = delete;

    [[nodiscard]] auto decode(const CapturedVideoFrame& captured_frame) -> DecodedFrame {
      if (captured_frame.data == nullptr || captured_frame.size_bytes == 0) {
        throw std::runtime_error("Captured MJPEG frame is empty");
      }

      ensure_input_capacity(captured_frame.size_bytes);
      auto* input_data = mpp_buffer_get_ptr_with_caller(input_buffer_, __func__);
      if (input_data == nullptr) {
        throw std::runtime_error("MPP: failed to map MJPEG input buffer");
      }

      check_mpp(mpp_buffer_sync_begin_f(input_buffer_, 0, __func__), "begin MJPEG input buffer access");
      std::memcpy(input_data, captured_frame.data, captured_frame.size_bytes);
      check_mpp(mpp_buffer_sync_end_f(input_buffer_, 0, __func__), "finish MJPEG input buffer access");

      auto packet = MppPacketHandle{};
      check_mpp(mpp_packet_init_with_buffer(packet.address(), input_buffer_), "create MJPEG input packet");
      mpp_packet_set_pos(packet.get(), input_data);
      mpp_packet_set_size(packet.get(), captured_frame.size_bytes);
      mpp_packet_set_length(packet.get(), captured_frame.size_bytes);

      const auto packet_metadata = mpp_packet_get_meta(packet.get());
      if (packet_metadata == nullptr) {
        throw std::runtime_error("MPP: failed to access MJPEG packet metadata");
      }
      check_mpp(mpp_meta_set_frame(packet_metadata, KEY_OUTPUT_FRAME, output_frame_),
                "attach preallocated MJPEG output frame");

      check_mpp(api_->decode_put_packet(context_, packet.get()), "submit MJPEG input packet");

      MppFrame decoded_frame = nullptr;
      const auto decode_result = api_->decode_get_frame(context_, &decoded_frame);
      if (decode_result != MPP_OK || decoded_frame == nullptr) {
        if (decoded_frame != nullptr && decoded_frame != output_frame_) {
          static_cast<void>(mpp_frame_deinit(&decoded_frame));
        }
        throw std::runtime_error("MPP: retrieve decoded MJPEG frame failed with status " +
                                 std::to_string(static_cast<int>(decode_result)));
      }

      if (decoded_frame != output_frame_) {
        static_cast<void>(mpp_frame_deinit(&decoded_frame));
        throw std::runtime_error("MPP: decoder did not return the preallocated MJPEG output frame");
      }

      validate_decoded_frame(decoded_frame);
      const auto decoded_buffer = mpp_frame_get_buffer(decoded_frame);
      const auto dma_fd = mpp_buffer_get_fd_with_caller(decoded_buffer, __func__);
      if (dma_fd < 0) {
        throw std::runtime_error("MPP: decoded MJPEG buffer does not expose a DMA file descriptor");
      }

      return DecodedFrame{dma_fd,
                          mpp_buffer_get_size_with_caller(decoded_buffer, __func__),
                          mpp_frame_get_width(decoded_frame),
                          mpp_frame_get_height(decoded_frame),
                          mpp_frame_get_hor_stride_pixel(decoded_frame),
                          mpp_frame_get_ver_stride(decoded_frame)};
    }

  private:
    void initialize() {
      check_mpp(mpp_buffer_group_get(&buffer_group_, MPP_BUFFER_TYPE_ION, MPP_BUFFER_INTERNAL, kMppBufferTag,
                                     __func__),
                "create DMA buffer group");

      const auto output_buffer_size = checked_output_buffer_size(capture_format_.width, capture_format_.height);
      check_mpp(mpp_buffer_get_with_tag(buffer_group_, &output_buffer_, output_buffer_size, kMppBufferTag, __func__),
                "allocate aligned MJPEG output buffer");
      check_mpp(mpp_frame_init(&output_frame_), "create MJPEG output frame");
      mpp_frame_set_buffer(output_frame_, output_buffer_);
      mpp_frame_set_width(output_frame_, capture_format_.width);
      mpp_frame_set_height(output_frame_, capture_format_.height);
      mpp_frame_set_fmt(output_frame_, MPP_FMT_RGB888);
      mpp_frame_set_buf_size(output_frame_, output_buffer_size);
      const auto aligned_width = checked_align_up(capture_format_.width, kMppOutputAlignment, "MJPEG width");
      const auto aligned_height = checked_align_up(capture_format_.height, kMppOutputAlignment, "MJPEG height");
      if (aligned_width > std::numeric_limits<std::uint32_t>::max() / kRgbBytesPerPixel) {
        throw std::runtime_error("MJPEG RGB stride exceeds MPP limits");
      }
      mpp_frame_set_hor_stride_pixel(output_frame_, aligned_width);
      mpp_frame_set_hor_stride(output_frame_, aligned_width * kRgbBytesPerPixel);
      mpp_frame_set_ver_stride(output_frame_, aligned_height);

      check_mpp(mpp_create(&context_, &api_), "create MJPEG decoder context");
      if (api_ == nullptr) {
        throw std::runtime_error("MPP: decoder API is unavailable");
      }
      check_mpp(mpp_init(context_, MPP_CTX_DEC, MPP_VIDEO_CodingMJPEG), "initialize MJPEG decoder");

      auto output_format = MPP_FMT_RGB888;
      check_mpp(api_->control(context_, MPP_DEC_SET_OUTPUT_FORMAT, &output_format),
                "configure MJPEG decoder for RGB888 output");

      auto output_timeout_ms = kMppDecodeTimeoutMs;
      check_mpp(api_->control(context_, MPP_SET_OUTPUT_TIMEOUT, &output_timeout_ms),
                "configure MJPEG decoder output timeout");
    }

    void ensure_input_capacity(std::size_t required_size) {
      if (input_buffer_ != nullptr && mpp_buffer_get_size_with_caller(input_buffer_, __func__) >= required_size) {
        return;
      }

      MppBuffer replacement = nullptr;
      check_mpp(mpp_buffer_get_with_tag(buffer_group_, &replacement, required_size, kMppBufferTag, __func__),
                "allocate MJPEG input buffer");
      if (input_buffer_ != nullptr) {
        static_cast<void>(mpp_buffer_put_with_caller(input_buffer_, __func__));
      }
      input_buffer_ = replacement;
    }

    void validate_decoded_frame(MppFrame decoded_frame) const {
      if (mpp_frame_get_info_change(decoded_frame) != 0) {
        throw std::runtime_error("MPP: unexpected MJPEG stream information change");
      }
      if (mpp_frame_get_errinfo(decoded_frame) != 0 || mpp_frame_get_discard(decoded_frame) != 0) {
        throw std::runtime_error("MPP: MJPEG hardware decoder rejected the captured frame");
      }
      if (mpp_frame_get_fmt(decoded_frame) != MPP_FMT_RGB888) {
        throw std::runtime_error("MPP: MJPEG decoder did not produce RGB888 output");
      }

      const auto width = mpp_frame_get_width(decoded_frame);
      const auto height = mpp_frame_get_height(decoded_frame);
      if (width != capture_format_.width || height != capture_format_.height) {
        throw std::runtime_error("MPP: MJPEG frame dimensions do not match negotiated capture format");
      }

      const auto horizontal_stride_pixels = mpp_frame_get_hor_stride_pixel(decoded_frame);
      const auto vertical_stride_pixels = mpp_frame_get_ver_stride(decoded_frame);
      if (horizontal_stride_pixels < width || vertical_stride_pixels < height) {
        throw std::runtime_error("MPP: decoded MJPEG frame has invalid RGB stride metadata");
      }

      if (mpp_frame_get_buffer(decoded_frame) != output_buffer_) {
        throw std::runtime_error("MPP: decoder did not use the preallocated MJPEG output buffer");
      }
    }

    void release() noexcept {
      if (output_frame_ != nullptr) {
        static_cast<void>(mpp_frame_deinit(&output_frame_));
      }
      if (context_ != nullptr) {
        static_cast<void>(mpp_destroy(context_));
        context_ = nullptr;
        api_ = nullptr;
      }
      if (input_buffer_ != nullptr) {
        static_cast<void>(mpp_buffer_put_with_caller(input_buffer_, __func__));
        input_buffer_ = nullptr;
      }
      if (output_buffer_ != nullptr) {
        static_cast<void>(mpp_buffer_put_with_caller(output_buffer_, __func__));
        output_buffer_ = nullptr;
      }
      if (buffer_group_ != nullptr) {
        static_cast<void>(mpp_buffer_group_put(buffer_group_));
        buffer_group_ = nullptr;
      }
    }

    VideoFormat capture_format_;
    MppCtx context_;
    MppApi* api_;
    MppBufferGroup buffer_group_;
    MppBuffer input_buffer_;
    MppBuffer output_buffer_;
    MppFrame output_frame_;
  };

  VideoProcessor::VideoProcessor(VideoFormat capture_format, VideoFormat output_format, bool mirror_output,
                                 std::uint32_t rotation_degrees) :
      capture_format_{capture_format}, output_format_{output_format}, mirror_output_{mirror_output},
      rotation_degrees_{rotation_degrees}, mjpeg_decoder_{nullptr} {
    if (output_format_.pixel_format != kPixelFormatRgb24) {
      throw std::runtime_error("Video output pixel format must be RGB24");
    }

    if (capture_format_.pixel_format != kPixelFormatYuyv && capture_format_.pixel_format != kPixelFormatMjpeg) {
      throw std::runtime_error("Video capture pixel format must be YUYV or MJPEG");
    }

    if (capture_format_.pixel_format == kPixelFormatYuyv && !is_even(capture_format_.width)) {
      throw std::runtime_error("YUYV capture width must be even");
    }

    if (capture_format_.pixel_format == kPixelFormatMjpeg) {
      mjpeg_decoder_ = std::make_unique<MjpegDecoder>(capture_format_);
    }
  }

  VideoProcessor::~VideoProcessor() = default;

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
    return checked_rgb_size_bytes(output_format_, "RGB output");
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

    // RGA hardware accelerator: YUYV→RGB conversion + resize, with optional transform.
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
    const auto src_rect =
        im_rect{0, 0, static_cast<int>(capture_format_.width), static_cast<int>(capture_format_.height)};
    const auto dst_rect =
        im_rect{0, 0, static_cast<int>(output_format_.width), static_cast<int>(output_format_.height)};
    constexpr auto empty_rect = im_rect{0, 0, 0, 0};
    const auto usage = rga_transform_usage(rotation_degrees_, mirror_output_);

    const auto status = improcess(src_img, dst_img, {}, src_rect, dst_rect, empty_rect, usage);

    releasebuffer_handle(dst_handle);
    releasebuffer_handle(src_handle);

    if (status != IM_STATUS_SUCCESS) {
      throw std::runtime_error(std::string{"RGA YUYV processing failed with status: "} +
                               std::to_string(static_cast<int>(status)));
    }
  }

  void VideoProcessor::mjpeg_to_resized_rgb(const CapturedVideoFrame& captured_frame,
                                            iox2::bb::MutableSlice<std::uint8_t> output_payload) const {
    if (mjpeg_decoder_ == nullptr) {
      throw std::runtime_error("MPP MJPEG decoder is not initialized");
    }

    const auto required_output_size = rgb_output_size_bytes();
    if (required_output_size > output_payload.number_of_elements()) {
      throw std::runtime_error("RGB video frame exceeds loaned output payload size");
    }

    // MPP decodes into a DMA-backed RGB888 frame; RGA consumes that DMA buffer directly.
    const auto decoded_frame = mjpeg_decoder_->decode(captured_frame);
    const auto src_buffer_size = checked_int(decoded_frame.buffer_size_bytes, "MPP MJPEG output buffer");
    const auto src_width = checked_int(decoded_frame.width, "Decoded MJPEG width");
    const auto src_height = checked_int(decoded_frame.height, "Decoded MJPEG height");
    const auto src_horizontal_stride =
        checked_int(decoded_frame.horizontal_stride_pixels, "Decoded MJPEG horizontal stride");
    const auto src_vertical_stride =
        checked_int(decoded_frame.vertical_stride_pixels, "Decoded MJPEG vertical stride");
    const auto usage = rga_transform_usage(rotation_degrees_, mirror_output_);
    const auto src_handle = importbuffer_fd(decoded_frame.dma_fd, src_buffer_size);
    if (src_handle == 0) {
      throw std::runtime_error("RGA: failed to import MPP MJPEG DMA buffer");
    }

    const auto dst_buffer_size = static_cast<int>(required_output_size);
    const auto dst_handle = importbuffer_virtualaddr(output_payload.data(), dst_buffer_size);
    if (dst_handle == 0) {
      releasebuffer_handle(src_handle);
      throw std::runtime_error("RGA: failed to import RGB destination buffer");
    }

    const auto src_img = wrapbuffer_handle(src_handle, src_width, src_height, RK_FORMAT_RGB_888,
                                           src_horizontal_stride, src_vertical_stride);
    const auto dst_img = wrapbuffer_handle(dst_handle, static_cast<int>(output_format_.width),
                                           static_cast<int>(output_format_.height), RK_FORMAT_RGB_888);
    const auto src_rect =
        im_rect{0, 0, static_cast<int>(capture_format_.width), static_cast<int>(capture_format_.height)};
    const auto dst_rect =
        im_rect{0, 0, static_cast<int>(output_format_.width), static_cast<int>(output_format_.height)};
    constexpr auto empty_rect = im_rect{0, 0, 0, 0};
    const auto status = improcess(src_img, dst_img, {}, src_rect, dst_rect, empty_rect, usage);

    releasebuffer_handle(dst_handle);
    releasebuffer_handle(src_handle);

    if (status != IM_STATUS_SUCCESS) {
      throw std::runtime_error(std::string{"RGA MPP RGB processing failed with status: "} +
                               std::to_string(static_cast<int>(status)));
    }
  }

} // namespace signlang::video_frontend
