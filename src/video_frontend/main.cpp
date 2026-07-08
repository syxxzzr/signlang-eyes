#include "common/runtime.hpp"
#include "program_options.hpp"
#include "spdlog/spdlog.h"
#include "v4l2_capture_device.hpp"
#include "video_processor.hpp"
#include "video_publisher.hpp"

#include <chrono>
#include <thread>

namespace {

  class CapturedFrameGuard {
  public:
    explicit CapturedFrameGuard(signlang::video_frontend::V4l2CaptureDevice& capture_device) :
        capture_device_{capture_device}, active_{true} {}

    CapturedFrameGuard(const CapturedFrameGuard&) = delete;
    auto operator=(const CapturedFrameGuard&) -> CapturedFrameGuard& = delete;

    ~CapturedFrameGuard() {
      if (!active_) {
        return;
      }

      try {
        capture_device_.release_frame();
      } catch (const std::exception& error) {
        spdlog::warn("Failed to release V4L2 frame during cleanup: {}", error.what());
      }
    }

    void release() {
      if (!active_) {
        return;
      }

      capture_device_.release_frame();
      active_ = false;
    }

  private:
    signlang::video_frontend::V4l2CaptureDevice& capture_device_;
    bool active_;
  };

  auto resolve_output_format(const signlang::video_frontend::VideoFormat& capture_format,
                             const signlang::video_frontend::VideoFormatRequest& output_request)
      -> signlang::video_frontend::VideoFormat {
    const auto output_width = output_request.width.value_or(capture_format.width);
    const auto output_height = output_request.height.value_or(capture_format.height);
    return signlang::video_frontend::VideoFormat{
        output_width, output_height, signlang::video_frontend::kPixelFormatRgb24};
  }

} // namespace

auto main(int argc, char** argv) -> int {
  using signlang::video_frontend::parse_program_options;
  using signlang::video_frontend::V4l2CaptureDevice;
  using signlang::video_frontend::VideoProcessor;
  using signlang::video_frontend::VideoPublisher;

  return signlang::runtime::run_module(argc, argv, parse_program_options, [&](const auto& options) {
    spdlog::info("Starting video frontend");
    spdlog::info("Device: {}", options.camera_device_name);
    if (options.capture_format.width.has_value() && options.capture_format.height.has_value()) {
      spdlog::info("Requested capture: {}x{} @ {}fps", options.capture_format.width.value(),
                   options.capture_format.height.value(), options.fps);
    }
    if (options.output_format.width.has_value() && options.output_format.height.has_value()) {
      spdlog::info("Requested output: {}x{}", options.output_format.width.value(),
                   options.output_format.height.value());
    }

    V4l2CaptureDevice capture_device{options.camera_device_name, options.capture_format, options.fps};
    const auto capture_format = capture_device.format();
    spdlog::info("Actual capture: {}x{} @ {}fps", capture_format.width, capture_format.height, capture_device.fps());

    const auto output_format = resolve_output_format(capture_format, options.output_format);
    spdlog::info("Actual output: {}x{}", output_format.width, output_format.height);
    spdlog::info("Mirror output: {}", options.mirror_output ? "enabled" : "disabled");
    spdlog::info("Output rotation: {} degrees clockwise", options.rotation_degrees);

    VideoProcessor video_processor{capture_format, output_format, options.mirror_output, options.rotation_degrees};
    VideoPublisher publisher{options.service_name,
                             video_processor.max_output_size_bytes(capture_device.max_frame_size_bytes())};

    std::uint64_t sequence_number = 0;
    auto downstream_active = false;
    while (!signlang::runtime::shutdown_requested()) {
      if (!publisher.has_subscribers()) {
        if (downstream_active) {
          spdlog::info("Video frontend downstream subscriber disconnected; pausing capture");
          downstream_active = false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
      }
      if (!downstream_active) {
        spdlog::info("Video frontend downstream subscriber connected; resuming capture");
        downstream_active = true;
      }

      const auto frame = capture_device.capture_frame();
      auto frame_guard = CapturedFrameGuard{capture_device};
      const auto current_sequence_number = sequence_number++;
      publisher.publish(frame, video_processor, capture_device.fps(), current_sequence_number);
      if (current_sequence_number % 300 == 0) {
        spdlog::debug("Published video frame sequence {}", current_sequence_number);
      }
      frame_guard.release();
    }

    spdlog::info("Video frontend stopped after publishing {} frames", sequence_number);
    return 0;
  });
}
