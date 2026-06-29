#include "program_options.hpp"

#include "common/logging_cli.hpp"
#include "cxxopts.hpp"

#include <stdexcept>
#include <string>

namespace signlang::signlang_manager {

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult {
    cxxopts::Options options{"signlang_eyes_signlang_manager",
                             "Expose handpose streaming and sign language prototype database management over BLE"};

    options.add_options()("i,input-service", "Input handpose iceoryx2 service name", cxxopts::value<std::string>())(
        "signlang-result-service", "signlang_det result publish-subscribe service name", cxxopts::value<std::string>())(
        "gesture-management-service", "signlang_det gesture management request-response service name",
        cxxopts::value<std::string>())("bluetooth-name", "BLE advertising local name",
                                       cxxopts::value<std::string>()->default_value(kDefaultBluetoothName))(
        "adapter-path", "BlueZ adapter object path", cxxopts::value<std::string>()->default_value(kDefaultAdapterPath))(
        "subscriber-buffer", "iceoryx2 subscriber queue size",
        cxxopts::value<std::uint64_t>()->default_value(std::to_string(kDefaultSubscriberBufferSize)))(
        "stream-fps", "Maximum BLE handpose streaming frame rate",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultStreamFps)))(
        "max-notify-payload", "Maximum BLE notification payload chunk size",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultMaxNotifyPayload)))(
        "max-upload-bytes", "Maximum BLE gesture upload size in bytes",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultMaxUploadBytes)))(
        "enable-streaming-by-default", "Start handpose streaming as soon as a BLE client subscribes")("h,help",
                                                                                                      "Print usage");
    signlang::logging::add_cli_options(options);

    const auto parsed_options = options.parse(argc, argv);
    if (parsed_options.count("help") != 0) {
      return ProgramUsage{.text = options.help()};
    }

    if (parsed_options.count("input-service") == 0 || parsed_options.count("signlang-result-service") == 0 ||
        parsed_options.count("gesture-management-service") == 0) {
      throw std::runtime_error(
          "Options --input-service, --signlang-result-service, and --gesture-management-service are required.\n\n" +
          options.help());
    }

    const auto subscriber_buffer_size = parsed_options["subscriber-buffer"].as<std::uint64_t>();
    if (subscriber_buffer_size == 0) {
      throw std::runtime_error("--subscriber-buffer must be greater than 0");
    }

    const auto stream_fps = parsed_options["stream-fps"].as<std::uint32_t>();
    if (stream_fps == 0) {
      throw std::runtime_error("--stream-fps must be greater than 0");
    }

    const auto max_notify_payload = parsed_options["max-notify-payload"].as<std::uint32_t>();
    if (max_notify_payload < 20) {
      throw std::runtime_error("--max-notify-payload must be at least 20");
    }

    const auto max_upload_bytes = parsed_options["max-upload-bytes"].as<std::uint32_t>();
    if (max_upload_bytes == 0) {
      throw std::runtime_error("--max-upload-bytes must be greater than 0");
    }

    return ProgramOptions{
        .input_service_name = parsed_options["input-service"].as<std::string>(),
        .signlang_result_service_name = parsed_options["signlang-result-service"].as<std::string>(),
        .gesture_management_service_name = parsed_options["gesture-management-service"].as<std::string>(),
        .bluetooth_name = parsed_options["bluetooth-name"].as<std::string>(),
        .adapter_path = parsed_options["adapter-path"].as<std::string>(),
        .subscriber_buffer_size = subscriber_buffer_size,
        .stream_fps = stream_fps,
        .max_notify_payload = max_notify_payload,
        .max_upload_bytes = max_upload_bytes,
        .enable_streaming_by_default = parsed_options.count("enable-streaming-by-default") != 0,
        .logging = signlang::logging::parse_cli_options(parsed_options),
    };
  }

} // namespace signlang::signlang_manager
