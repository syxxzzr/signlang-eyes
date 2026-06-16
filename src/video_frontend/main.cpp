#include "program_options.hpp"
#include "v4l2_capture_device.hpp"
#include "video_processor.hpp"
#include "video_publisher.hpp"

#include <csignal>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <variant>

namespace {

  volatile std::sig_atomic_t g_should_stop = 0;

  void handle_shutdown_signal(int /* signal_number */) { g_should_stop = 1; }

  void install_signal_handlers() {
    std::signal(SIGINT, handle_shutdown_signal);
    std::signal(SIGTERM, handle_shutdown_signal);
  }

  auto resolve_output_format(const signlang::video_frontend::VideoFormat& capture_format,
                             const signlang::video_frontend::VideoFormatRequest& output_request)
      -> signlang::video_frontend::VideoFormat {
    const auto output_width = output_request.width.value_or(capture_format.width);
    const auto output_height = output_request.height.value_or(capture_format.height);
    return signlang::video_frontend::VideoFormat{
        .width = output_width,
        .height = output_height,
        .pixel_format = capture_format.pixel_format,
    };
  }

} // namespace

auto main(int argc, char** argv) -> int {
  using signlang::video_frontend::parse_program_options;
  using signlang::video_frontend::ProgramOptions;
  using signlang::video_frontend::ProgramUsage;
  using signlang::video_frontend::V4l2CaptureDevice;
  using signlang::video_frontend::VideoProcessor;
  using signlang::video_frontend::VideoPublisher;

  try {
    const auto parse_result = parse_program_options(argc, argv);
    if (const auto* usage = std::get_if<ProgramUsage>(&parse_result); usage != nullptr) {
      std::cout << usage->text << '\n';
      return 0;
    }

    const auto& options = std::get<ProgramOptions>(parse_result);
    install_signal_handlers();

    V4l2CaptureDevice capture_device{options.camera_device_name, options.capture_format, options.fps};
    const auto capture_format = capture_device.format();
    const auto output_format = resolve_output_format(capture_format, options.output_format);
    VideoProcessor video_processor{capture_format, output_format};
    VideoPublisher publisher{options.service_name,
                             video_processor.max_output_size_bytes(capture_device.max_frame_size_bytes())};

    std::uint64_t sequence_number = 0;
    while (g_should_stop == 0) {
      const auto frame = capture_device.capture_frame();
      publisher.publish(frame, video_processor, capture_device.fps(), sequence_number++);
      capture_device.release_frame();
    }

    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
