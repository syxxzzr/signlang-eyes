#include "video_frame.hpp"

#include "cxxopts.hpp"
#include "iox2/iceoryx2.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

  constexpr auto kDefaultTimeoutSec = std::uint32_t{10};

  struct ProgramOptions {
    std::string service_name;
    std::optional<std::string> output_path;
    std::uint32_t timeout_sec;
    std::uint64_t subscriber_buffer_size;
  };

  struct VideoSample {
    signlang::video_frontend::VideoFrameMetadata metadata;
    std::vector<std::uint8_t> payload;
  };

  auto parse_options(int argc, char** argv) -> ProgramOptions {
    cxxopts::Options options{
        "signlang_eyes_video_frontend_snapshot_test",
        "Subscribe to a video_frontend iceoryx2 service and save the first received frame as an image."};

    options.add_options()("s,service", "iceoryx2 video service name", cxxopts::value<std::string>())(
        "o,output", "Output image file path", cxxopts::value<std::string>())(
        "timeout-sec", "Seconds to wait for the first frame",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultTimeoutSec)))(
        "buffer-size", "iceoryx2 subscriber buffer size", cxxopts::value<std::uint64_t>()->default_value("4"))(
        "h,help", "Print usage");

    const auto parsed_options = options.parse(argc, argv);
    if (parsed_options.count("help") != 0) {
      std::cout << options.help() << '\n';
      std::exit(0);
    }

    if (parsed_options.count("service") == 0) {
      throw std::runtime_error("--service is required.\n\n" + options.help());
    }

    const auto timeout_sec = parsed_options["timeout-sec"].as<std::uint32_t>();
    if (timeout_sec == 0) {
      throw std::runtime_error("--timeout-sec must be greater than zero");
    }

    const auto subscriber_buffer_size = parsed_options["buffer-size"].as<std::uint64_t>();
    if (subscriber_buffer_size == 0) {
      throw std::runtime_error("--buffer-size must be greater than zero");
    }

    return ProgramOptions{
        .service_name = parsed_options["service"].as<std::string>(),
        .output_path =
            parsed_options.count("output") == 0 ? std::nullopt : std::make_optional(parsed_options["output"].as<std::string>()),
        .timeout_sec = timeout_sec,
        .subscriber_buffer_size = subscriber_buffer_size,
    };
  }

  auto service_name_from_string(const std::string& service_name) -> iox2::ServiceName {
    const auto parsed_service_name = iox2::ServiceName::create(service_name.c_str());
    if (!parsed_service_name.has_value()) {
      throw std::runtime_error("Invalid iceoryx2 service name: " + service_name);
    }

    return parsed_service_name.value();
  }

  auto clamp_to_byte(int value) -> std::uint8_t {
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
  }

  void append_yuv_as_rgb(std::vector<std::uint8_t>& rgb, std::uint8_t y, std::uint8_t u, std::uint8_t v) {
    const auto c = static_cast<int>(y) - 16;
    const auto d = static_cast<int>(u) - 128;
    const auto e = static_cast<int>(v) - 128;

    rgb.push_back(clamp_to_byte((298 * c + 409 * e + 128) >> 8));
    rgb.push_back(clamp_to_byte((298 * c - 100 * d - 208 * e + 128) >> 8));
    rgb.push_back(clamp_to_byte((298 * c + 516 * d + 128) >> 8));
  }

  auto yuyv_to_rgb(const std::uint8_t* payload, std::uint64_t payload_size, std::uint32_t width, std::uint32_t height)
      -> std::vector<std::uint8_t> {
    if ((width % 2U) != 0U) {
      throw std::runtime_error("YUYV snapshot requires an even image width");
    }

    const auto required_size = static_cast<std::uint64_t>(width) * height * 2U;
    if (payload_size < required_size) {
      throw std::runtime_error("YUYV payload is smaller than expected for metadata dimensions");
    }

    std::vector<std::uint8_t> rgb;
    rgb.reserve(static_cast<std::uint64_t>(width) * height * 3U);

    for (std::uint64_t offset = 0; offset < required_size; offset += 4U) {
      const auto y0 = payload[offset];
      const auto u = payload[offset + 1U];
      const auto y1 = payload[offset + 2U];
      const auto v = payload[offset + 3U];
      append_yuv_as_rgb(rgb, y0, u, v);
      append_yuv_as_rgb(rgb, y1, u, v);
    }

    return rgb;
  }

  void write_binary_file(const std::string& output_path, const std::uint8_t* data, std::uint64_t size_bytes) {
    std::ofstream output{output_path, std::ios::binary};
    if (!output.is_open()) {
      throw std::runtime_error("Failed to open output image file: " + output_path);
    }

    output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size_bytes));
    if (!output.good()) {
      throw std::runtime_error("Failed while writing output image file: " + output_path);
    }
  }

  void write_ppm_file(const std::string& output_path, const signlang::video_frontend::VideoFrameMetadata& metadata,
                      const std::uint8_t* payload, std::uint64_t payload_size) {
    const auto rgb = yuyv_to_rgb(payload, payload_size, metadata.output_width, metadata.output_height);

    std::ofstream output{output_path, std::ios::binary};
    if (!output.is_open()) {
      throw std::runtime_error("Failed to open PPM output file: " + output_path);
    }

    output << "P6\n" << metadata.output_width << ' ' << metadata.output_height << "\n255\n";
    output.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    if (!output.good()) {
      throw std::runtime_error("Failed while writing PPM output file: " + output_path);
    }
  }

  auto resolve_output_path(const std::optional<std::string>& requested_output_path,
                           const signlang::video_frontend::VideoFrameMetadata& metadata) -> std::string {
    if (requested_output_path.has_value()) {
      return requested_output_path.value();
    }

    if (metadata.pixel_format == signlang::video_frontend::kPixelFormatMjpeg) {
      return "video_frontend_snapshot.jpg";
    }

    return "video_frontend_snapshot.ppm";
  }

  auto save_snapshot(const std::optional<std::string>& requested_output_path, const VideoSample& sample) -> std::string {
    if (sample.metadata.payload_size_bytes > sample.payload.size()) {
      throw std::runtime_error("Video metadata payload size exceeds received sample size");
    }

    const auto output_path = resolve_output_path(requested_output_path, sample.metadata);

    if (sample.metadata.pixel_format == signlang::video_frontend::kPixelFormatMjpeg) {
      write_binary_file(output_path, sample.payload.data(), sample.metadata.payload_size_bytes);
      return output_path;
    }

    if (sample.metadata.pixel_format == signlang::video_frontend::kPixelFormatYuyv) {
      write_ppm_file(output_path, sample.metadata, sample.payload.data(), sample.metadata.payload_size_bytes);
      return output_path;
    }

    throw std::runtime_error("Unsupported video pixel format in snapshot payload");
  }

  class VideoSubscriber {
  public:
    VideoSubscriber(const std::string& service_name, std::uint64_t subscriber_buffer_size) :
        node_{create_node()}, subscriber_{create_subscriber(node_, service_name, subscriber_buffer_size)} {}

    auto receive_one() -> std::optional<VideoSample> {
      auto receive_result = subscriber_.receive();
      if (!receive_result.has_value()) {
        throw std::runtime_error("Failed to receive iceoryx2 video frame sample");
      }

      auto sample = std::move(receive_result.value());
      if (!sample.has_value()) {
        return std::nullopt;
      }

      const auto payload = sample->payload();
      return VideoSample{
          .metadata = sample->user_header(),
          .payload = std::vector<std::uint8_t>{payload.data(), payload.data() + payload.number_of_elements()},
      };
    }

  private:
    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
      iox2::set_log_level_from_env_or(iox2::LogLevel::Warn);

      auto node = iox2::NodeBuilder().create<iox2::ServiceType::Ipc>();
      if (!node.has_value()) {
        throw std::runtime_error("Failed to create iceoryx2 IPC node");
      }

      return std::move(node.value());
    }

    static auto create_subscriber(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name,
                                  std::uint64_t subscriber_buffer_size)
        -> iox2::Subscriber<iox2::ServiceType::Ipc, iox2::bb::Slice<std::uint8_t>,
                            signlang::video_frontend::VideoFrameMetadata> {
      auto service = node.service_builder(service_name_from_string(service_name))
                         .publish_subscribe<iox2::bb::Slice<std::uint8_t>>()
                         .user_header<signlang::video_frontend::VideoFrameMetadata>()
                         .open_or_create();
      if (!service.has_value()) {
        throw std::runtime_error("Failed to open or create iceoryx2 video service: " + service_name);
      }

      auto subscriber = service.value().subscriber_builder().buffer_size(subscriber_buffer_size).create();
      if (!subscriber.has_value()) {
        throw std::runtime_error("Failed to create iceoryx2 video subscriber for service: " + service_name);
      }

      return std::move(subscriber.value());
    }

    iox2::Node<iox2::ServiceType::Ipc> node_;
    iox2::Subscriber<iox2::ServiceType::Ipc, iox2::bb::Slice<std::uint8_t>,
                     signlang::video_frontend::VideoFrameMetadata>
        subscriber_;
  };

} // namespace

auto main(int argc, char** argv) -> int {
  try {
    const auto options = parse_options(argc, argv);
    VideoSubscriber subscriber{options.service_name, options.subscriber_buffer_size};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{options.timeout_sec};

    while (std::chrono::steady_clock::now() < deadline) {
      auto sample = subscriber.receive_one();
      if (sample.has_value()) {
        const auto output_path = save_snapshot(options.output_path, sample.value());
        std::cout << "Saved video snapshot to " << output_path << '\n';
        return 0;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }

    throw std::runtime_error("Timed out waiting for a video frame from service: " + options.service_name);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
