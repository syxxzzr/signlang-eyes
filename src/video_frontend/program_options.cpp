#include "program_options.hpp"

#include "common/cpu_affinity_cli.hpp"
#include "common/logging_cli.hpp"
#include "cxxopts.hpp"

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
      if (value == 0) {
        throw std::runtime_error(std::string("--") + option_name + " must be greater than 0");
      }

      return value;
    }

    void validate_dimension_pair(const VideoFormatRequest& request, const char* width_name, const char* height_name) {
      if (request.width.has_value() != request.height.has_value()) {
        throw std::runtime_error(std::string("--") + width_name + " and --" + height_name +
                                 " must be specified together");
      }
    }

    void validate_rotation_degrees(std::uint32_t rotation_degrees) {
      if (rotation_degrees != 0 && rotation_degrees != 90 && rotation_degrees != 180 && rotation_degrees != 270) {
        throw std::runtime_error("--rotation must be one of: 0, 90, 180, 270");
      }
    }

  } // namespace

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult {
    cxxopts::Options options{
        "signlang_eyes_video_frontend",
        "Capture video frames from a V4L2 camera and publish them through an iceoryx2 publish-subscribe service."};

    options.add_options()("d,device", "V4L2 camera device name", cxxopts::value<std::string>())(
        "s,service", "iceoryx2 publish-subscribe service name", cxxopts::value<std::string>())(
        "capture-width", "Requested camera capture width in pixels", cxxopts::value<std::uint32_t>())(
        "capture-height", "Requested camera capture height in pixels",
        cxxopts::value<std::uint32_t>())("fps", "Requested camera frame rate",
                                         cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultFps)))(
        "output-width", "Published output width in pixels", cxxopts::value<std::uint32_t>())(
        "output-height", "Published output height in pixels", cxxopts::value<std::uint32_t>())(
        "mirror-output", "Horizontally mirror the published RGB output frame",
        cxxopts::value<bool>()->default_value("false")->implicit_value("true"))(
        "rotation", "Clockwise rotation applied to the published RGB output frame: 0, 90, 180, or 270 degrees",
        cxxopts::value<std::uint32_t>()->default_value("0"))("h,help", "Print usage");
    signlang::logging::add_cli_options(options);
    signlang::runtime::add_cpu_affinity_cli_options(options);

    const auto parsed_options = options.parse(argc, argv);
    if (parsed_options.count("help") != 0) {
      return ProgramUsage{options.help()};
    }

    if (parsed_options.count("device") == 0 || parsed_options.count("service") == 0) {
      throw std::runtime_error("Both --device and --service are required.\n\n" + options.help());
    }

    const auto fps = parsed_options["fps"].as<std::uint32_t>();
    if (fps == 0) {
      throw std::runtime_error("--fps must be greater than 0.\n\n" + options.help());
    }

    const VideoFormatRequest capture_format{optional_dimension(parsed_options, "capture-width"),
                                            optional_dimension(parsed_options, "capture-height")};
    const VideoFormatRequest output_format{optional_dimension(parsed_options, "output-width"),
                                           optional_dimension(parsed_options, "output-height")};

    validate_dimension_pair(capture_format, "capture-width", "capture-height");
    validate_dimension_pair(output_format, "output-width", "output-height");
    const auto rotation_degrees = parsed_options["rotation"].as<std::uint32_t>();
    validate_rotation_degrees(rotation_degrees);

    return ProgramOptionsParseResult{ProgramOptions{parsed_options["device"].as<std::string>(),
                                                    parsed_options["service"].as<std::string>(),
                                                    capture_format,
                                                    output_format,
                                                    fps,
                                                    parsed_options["mirror-output"].as<bool>(),
                                                    rotation_degrees,
                                                    signlang::logging::parse_cli_options(parsed_options),
                                                    signlang::runtime::parse_cpu_affinity_cli_options(parsed_options)}};
  }

} // namespace signlang::video_frontend
