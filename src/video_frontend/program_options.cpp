#include "program_options.hpp"

#include "cxxopts.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace signlang::video_frontend {
  namespace {

    auto optional_dimension(const cxxopts::ParseResult& parsed_options, const char* option_name)
        -> std::optional<std::uint32_t> {
      if (parsed_options.count(option_name) == 0) {
        return std::nullopt;
      }

      const auto value = parsed_options[option_name].as<std::uint32_t>();
      if (value < kMinDimension || value > kMaxDimension) {
        throw std::runtime_error(std::string("--") + option_name + " must be between " +
                                 std::to_string(kMinDimension) + " and " + std::to_string(kMaxDimension));
      }

      return value;
    }

    void validate_dimension_pair(const VideoFormatRequest& request, const char* width_name, const char* height_name) {
      if (request.width.has_value() != request.height.has_value()) {
        throw std::runtime_error(std::string("--") + width_name + " and --" + height_name +
                                 " must be specified together");
      }
    }

  } // namespace

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult {
    cxxopts::Options options{
        "signlang_eyes_edgeai_video_frontend",
        "Capture video frames from a V4L2 camera and publish them through an iceoryx2 publish-subscribe service."};

    options.add_options()("d,device", "V4L2 camera device name", cxxopts::value<std::string>())(
        "s,service", "iceoryx2 publish-subscribe service name", cxxopts::value<std::string>())(
        "capture-width", "Requested camera capture width in pixels", cxxopts::value<std::uint32_t>())(
        "capture-height", "Requested camera capture height in pixels", cxxopts::value<std::uint32_t>())(
        "fps", "Requested camera frame rate",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultFps)))(
        "output-width", "Published output width in pixels", cxxopts::value<std::uint32_t>())(
        "output-height", "Published output height in pixels", cxxopts::value<std::uint32_t>())("h,help", "Print usage");

    const auto parsed_options = options.parse(argc, argv);
    if (parsed_options.count("help") != 0) {
      return ProgramUsage{.text = options.help()};
    }

    if (parsed_options.count("device") == 0 || parsed_options.count("service") == 0) {
      throw std::runtime_error("Both --device and --service are required.\n\n" + options.help());
    }

    const auto fps = parsed_options["fps"].as<std::uint32_t>();
    if (fps == 0 || fps > kMaxFps) {
      throw std::runtime_error("--fps must be between 1 and " + std::to_string(kMaxFps) + ".\n\n" + options.help());
    }

    const VideoFormatRequest capture_format{
        .width = optional_dimension(parsed_options, "capture-width"),
        .height = optional_dimension(parsed_options, "capture-height"),
    };
    const VideoFormatRequest output_format{
        .width = optional_dimension(parsed_options, "output-width"),
        .height = optional_dimension(parsed_options, "output-height"),
    };

    validate_dimension_pair(capture_format, "capture-width", "capture-height");
    validate_dimension_pair(output_format, "output-width", "output-height");

    return ProgramOptionsParseResult{ProgramOptions{
        .camera_device_name = parsed_options["device"].as<std::string>(),
        .service_name = parsed_options["service"].as<std::string>(),
        .capture_format = capture_format,
        .output_format = output_format,
        .fps = fps,
    }};
  }

} // namespace signlang::video_frontend
