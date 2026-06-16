#include "v4l2_capture_device.hpp"

#include "video_frame.hpp"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace signlang::video_frontend {
  namespace {

    constexpr auto kRequestedBufferCount = std::uint32_t{4};
    constexpr auto kSelectTimeoutSeconds = long{2};

    auto v4l2_error_message(const std::string& context) -> std::string {
      return context + ": " + std::strerror(errno);
    }

    auto retry_ioctl(int fd, unsigned long request, void* arg) -> int {
      int result = 0;
      do {
        result = ioctl(fd, request, arg);
      } while (result == -1 && errno == EINTR);

      return result;
    }

    auto pixel_format_name(std::uint32_t pixel_format) -> const char* {
      switch (pixel_format) {
      case kPixelFormatYuyv:
        return "YUYV";
      case kPixelFormatMjpeg:
        return "MJPEG";
      default:
        return "unknown";
      }
    }

    auto frame_area(std::uint32_t width, std::uint32_t height) -> std::uint64_t {
      return static_cast<std::uint64_t>(width) * height;
    }

    auto is_preferred_format(VideoFormat candidate, VideoFormat selected_format) -> bool {
      const auto candidate_area = frame_area(candidate.width, candidate.height);
      const auto selected_area = frame_area(selected_format.width, selected_format.height);
      return candidate_area > selected_area ||
             (candidate_area == selected_area && candidate.pixel_format == kPixelFormatYuyv &&
              selected_format.pixel_format != kPixelFormatYuyv);
    }

    auto matches_stepwise_size(const v4l2_frmsize_stepwise& stepwise, std::uint32_t width, std::uint32_t height) -> bool {
      if (width < stepwise.min_width || width > stepwise.max_width || height < stepwise.min_height ||
          height > stepwise.max_height) {
        return false;
      }

      const auto width_step = stepwise.step_width == 0 ? std::uint32_t{1} : stepwise.step_width;
      const auto height_step = stepwise.step_height == 0 ? std::uint32_t{1} : stepwise.step_height;
      return ((width - stepwise.min_width) % width_step) == 0 && ((height - stepwise.min_height) % height_step) == 0;
    }

  } // namespace

  V4l2CaptureDevice::V4l2CaptureDevice(const std::string& device_name, VideoFormatRequest format_request,
                                       std::uint32_t fps) :
      device_name_{device_name}, format_request_{format_request}, requested_fps_{fps}, device_fd_{-1},
      format_{.width = 0, .height = 0, .pixel_format = kPixelFormatYuyv}, fps_{fps}, max_frame_size_bytes_{0},
      active_buffer_index_{-1}, streaming_{false} {
    open_device();
    configure();
    start_streaming();
  }

  V4l2CaptureDevice::~V4l2CaptureDevice() {
    stop_streaming();
    unmap_buffers();
    close_device();
  }

  auto V4l2CaptureDevice::format() const -> VideoFormat { return format_; }

  auto V4l2CaptureDevice::fps() const -> std::uint32_t { return fps_; }

  auto V4l2CaptureDevice::max_frame_size_bytes() const -> std::uint32_t { return max_frame_size_bytes_; }

  auto V4l2CaptureDevice::capture_frame() -> CapturedVideoFrame {
    if (active_buffer_index_ >= 0) {
      release_frame();
    }

    return dequeue_frame();
  }

  void V4l2CaptureDevice::release_frame() {
    if (active_buffer_index_ < 0) {
      return;
    }

    enqueue_buffer(static_cast<std::uint32_t>(active_buffer_index_));
    active_buffer_index_ = -1;
  }

  void V4l2CaptureDevice::open_device() {
    device_fd_ = ::open(device_name_.c_str(), O_RDWR | O_CLOEXEC);
    if (device_fd_ == -1) {
      throw std::runtime_error(v4l2_error_message("Failed to open V4L2 camera '" + device_name_ + "'"));
    }
  }

  void V4l2CaptureDevice::configure() {
    select_format();
    configure_format();
    configure_fps();
    configure_buffers();
  }

  void V4l2CaptureDevice::select_format() {
    v4l2_fmtdesc format_description{};
    format_description.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    auto selected_format = VideoFormat{.width = 0, .height = 0, .pixel_format = kPixelFormatYuyv};
    auto requested_format = VideoFormat{.width = 0, .height = 0, .pixel_format = kPixelFormatYuyv};
    for (format_description.index = 0; retry_ioctl(device_fd_, VIDIOC_ENUM_FMT, &format_description) == 0;
         ++format_description.index) {
      if (format_description.pixelformat != kPixelFormatYuyv && format_description.pixelformat != kPixelFormatMjpeg) {
        continue;
      }

      const auto candidate_format = select_largest_frame_size(format_description.pixelformat);
      if (is_preferred_format(candidate_format, selected_format)) {
        selected_format = candidate_format;
      }

      if (format_request_.width.has_value() &&
          supports_frame_size(format_description.pixelformat, format_request_.width.value(),
                              format_request_.height.value())) {
        const auto candidate_requested_format = VideoFormat{
            .width = format_request_.width.value(),
            .height = format_request_.height.value(),
            .pixel_format = format_description.pixelformat,
        };
        if (is_preferred_format(candidate_requested_format, requested_format)) {
          requested_format = candidate_requested_format;
        }
      }
    }

    if (selected_format.width == 0 || selected_format.height == 0) {
      throw std::runtime_error("V4L2 camera must support YUYV or MJPEG capture");
    }

    if (!format_request_.width.has_value()) {
      format_ = selected_format;
      return;
    }

    if (requested_format.width == 0 || requested_format.height == 0) {
      throw std::runtime_error("V4L2 camera does not support requested capture resolution " +
                               std::to_string(format_request_.width.value()) + "x" +
                               std::to_string(format_request_.height.value()) + " in YUYV or MJPEG");
    }

    format_ = requested_format;
  }

  auto V4l2CaptureDevice::select_largest_frame_size(std::uint32_t pixel_format) const -> VideoFormat {
    v4l2_frmsizeenum frame_size{};
    frame_size.pixel_format = pixel_format;

    auto selected_format = VideoFormat{.width = 0, .height = 0, .pixel_format = pixel_format};
    for (frame_size.index = 0; retry_ioctl(device_fd_, VIDIOC_ENUM_FRAMESIZES, &frame_size) == 0; ++frame_size.index) {
      if (frame_size.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
        if (frame_area(frame_size.discrete.width, frame_size.discrete.height) >
            frame_area(selected_format.width, selected_format.height)) {
          selected_format.width = frame_size.discrete.width;
          selected_format.height = frame_size.discrete.height;
        }
      } else if (frame_size.type == V4L2_FRMSIZE_TYPE_STEPWISE ||
                 frame_size.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
        if (frame_area(frame_size.stepwise.max_width, frame_size.stepwise.max_height) >
            frame_area(selected_format.width, selected_format.height)) {
          selected_format.width = frame_size.stepwise.max_width;
          selected_format.height = frame_size.stepwise.max_height;
        }
      }
    }

    if (selected_format.width == 0 || selected_format.height == 0) {
      throw std::runtime_error(std::string("Failed to enumerate V4L2 frame sizes for ") + pixel_format_name(pixel_format));
    }

    return selected_format;
  }

  auto V4l2CaptureDevice::supports_frame_size(std::uint32_t pixel_format, std::uint32_t width,
                                              std::uint32_t height) const -> bool {
    v4l2_frmsizeenum frame_size{};
    frame_size.pixel_format = pixel_format;

    for (frame_size.index = 0; retry_ioctl(device_fd_, VIDIOC_ENUM_FRAMESIZES, &frame_size) == 0; ++frame_size.index) {
      if (frame_size.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
        if (frame_size.discrete.width == width && frame_size.discrete.height == height) {
          return true;
        }
      } else if (frame_size.type == V4L2_FRMSIZE_TYPE_STEPWISE ||
                 frame_size.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
        if (matches_stepwise_size(frame_size.stepwise, width, height)) {
          return true;
        }
      }
    }

    return false;
  }

  void V4l2CaptureDevice::configure_format() {
    v4l2_format selected_format{};
    selected_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    selected_format.fmt.pix.width = format_.width;
    selected_format.fmt.pix.height = format_.height;
    selected_format.fmt.pix.pixelformat = format_.pixel_format;
    selected_format.fmt.pix.field = V4L2_FIELD_NONE;

    if (retry_ioctl(device_fd_, VIDIOC_S_FMT, &selected_format) == -1) {
      throw std::runtime_error(v4l2_error_message("Failed to set V4L2 capture format"));
    }

    if (selected_format.fmt.pix.pixelformat != format_.pixel_format) {
      throw std::runtime_error("V4L2 camera changed requested pixel format from " +
                               std::string(pixel_format_name(format_.pixel_format)));
    }

    if (format_request_.width.has_value() &&
        (selected_format.fmt.pix.width != format_.width || selected_format.fmt.pix.height != format_.height)) {
      throw std::runtime_error("V4L2 camera does not support requested capture resolution " +
                               std::to_string(format_.width) + "x" + std::to_string(format_.height));
    }

    format_.width = selected_format.fmt.pix.width;
    format_.height = selected_format.fmt.pix.height;
    max_frame_size_bytes_ = selected_format.fmt.pix.sizeimage;
    if (max_frame_size_bytes_ == 0) {
      throw std::runtime_error("V4L2 camera reported zero frame buffer size");
    }
  }

  void V4l2CaptureDevice::configure_fps() {
    v4l2_streamparm stream_params{};
    stream_params.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    stream_params.parm.capture.timeperframe.numerator = 1;
    stream_params.parm.capture.timeperframe.denominator = requested_fps_;

    if (retry_ioctl(device_fd_, VIDIOC_S_PARM, &stream_params) == -1) {
      throw std::runtime_error(v4l2_error_message("Failed to set V4L2 capture frame rate"));
    }

    const auto numerator = stream_params.parm.capture.timeperframe.numerator;
    const auto denominator = stream_params.parm.capture.timeperframe.denominator;
    if (numerator == 0 || denominator == 0) {
      fps_ = requested_fps_;
      return;
    }

    fps_ = denominator / numerator;
    if (fps_ == 0) {
      throw std::runtime_error("V4L2 camera selected an invalid capture frame rate");
    }

    if (fps_ != requested_fps_) {
      throw std::runtime_error("V4L2 camera does not support requested capture frame rate " +
                               std::to_string(requested_fps_) + " fps");
    }
  }

  void V4l2CaptureDevice::configure_buffers() {
    v4l2_requestbuffers request_buffers{};
    request_buffers.count = kRequestedBufferCount;
    request_buffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request_buffers.memory = V4L2_MEMORY_MMAP;

    if (retry_ioctl(device_fd_, VIDIOC_REQBUFS, &request_buffers) == -1) {
      throw std::runtime_error(v4l2_error_message("Failed to request V4L2 mmap buffers"));
    }

    if (request_buffers.count < 2) {
      throw std::runtime_error("V4L2 camera returned too few mmap buffers");
    }

    mapped_buffers_.resize(request_buffers.count);
    for (std::uint32_t buffer_index = 0; buffer_index < request_buffers.count; ++buffer_index) {
      v4l2_buffer buffer{};
      buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buffer.memory = V4L2_MEMORY_MMAP;
      buffer.index = buffer_index;

      if (retry_ioctl(device_fd_, VIDIOC_QUERYBUF, &buffer) == -1) {
        throw std::runtime_error(v4l2_error_message("Failed to query V4L2 mmap buffer"));
      }

      auto* mapped_data = mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd_, buffer.m.offset);
      if (mapped_data == MAP_FAILED) {
        throw std::runtime_error(v4l2_error_message("Failed to mmap V4L2 buffer"));
      }

      mapped_buffers_[buffer_index] = MappedBuffer{.start = mapped_data, .length = buffer.length};
      enqueue_buffer(buffer_index);
    }
  }

  void V4l2CaptureDevice::start_streaming() {
    auto buffer_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (retry_ioctl(device_fd_, VIDIOC_STREAMON, &buffer_type) == -1) {
      throw std::runtime_error(v4l2_error_message("Failed to start V4L2 capture stream"));
    }

    streaming_ = true;
  }

  void V4l2CaptureDevice::stop_streaming() noexcept {
    if (!streaming_ || device_fd_ == -1) {
      return;
    }

    auto buffer_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    static_cast<void>(retry_ioctl(device_fd_, VIDIOC_STREAMOFF, &buffer_type));
    streaming_ = false;
  }

  void V4l2CaptureDevice::close_device() noexcept {
    if (device_fd_ != -1) {
      static_cast<void>(::close(device_fd_));
      device_fd_ = -1;
    }
  }

  void V4l2CaptureDevice::unmap_buffers() noexcept {
    for (const auto& mapped_buffer : mapped_buffers_) {
      if (mapped_buffer.start != nullptr && mapped_buffer.start != MAP_FAILED) {
        static_cast<void>(munmap(mapped_buffer.start, mapped_buffer.length));
      }
    }

    mapped_buffers_.clear();
  }

  void V4l2CaptureDevice::enqueue_buffer(std::uint32_t buffer_index) {
    v4l2_buffer buffer{};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = buffer_index;

    if (retry_ioctl(device_fd_, VIDIOC_QBUF, &buffer) == -1) {
      throw std::runtime_error(v4l2_error_message("Failed to enqueue V4L2 buffer"));
    }
  }

  auto V4l2CaptureDevice::dequeue_frame() -> CapturedVideoFrame {
    while (true) {
      fd_set read_fds;
      FD_ZERO(&read_fds);
      FD_SET(device_fd_, &read_fds);

      timeval timeout{};
      timeout.tv_sec = kSelectTimeoutSeconds;

      const auto select_result = select(device_fd_ + 1, &read_fds, nullptr, nullptr, &timeout);
      if (select_result == -1) {
        if (errno == EINTR) {
          continue;
        }

        throw std::runtime_error(v4l2_error_message("Failed while waiting for V4L2 frame"));
      }

      if (select_result == 0) {
        throw std::runtime_error("Timed out waiting for V4L2 frame");
      }

      v4l2_buffer buffer{};
      buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buffer.memory = V4L2_MEMORY_MMAP;

      if (retry_ioctl(device_fd_, VIDIOC_DQBUF, &buffer) == -1) {
        if (errno == EAGAIN) {
          continue;
        }

        throw std::runtime_error(v4l2_error_message("Failed to dequeue V4L2 frame"));
      }

      active_buffer_index_ = static_cast<std::int32_t>(buffer.index);
      const auto& mapped_buffer = mapped_buffers_[buffer.index];
      return CapturedVideoFrame{
          .data = static_cast<const std::uint8_t*>(mapped_buffer.start),
          .size_bytes = buffer.bytesused,
      };
    }
  }

} // namespace signlang::video_frontend
