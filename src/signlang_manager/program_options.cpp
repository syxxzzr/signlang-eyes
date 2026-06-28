#include "program_options.hpp"

#include "common/logging_cli.hpp"
#include "cxxopts.hpp"

#include <stdexcept>
#include <string>

namespace signlang::signlang_manager {

  auto parse_npu_core_mask(const std::string& core_str) -> rknn_core_mask {
    if (core_str == "auto") {
      return RKNN_NPU_CORE_AUTO;
    }
    if (core_str == "0") {
      return RKNN_NPU_CORE_0;
    }
    if (core_str == "1") {
      return RKNN_NPU_CORE_1;
    }
    if (core_str == "2") {
      return RKNN_NPU_CORE_2;
    }
    if (core_str == "0_1") {
      return RKNN_NPU_CORE_0_1;
    }
    if (core_str == "0_1_2") {
      return RKNN_NPU_CORE_0_1_2;
    }
    if (core_str == "all") {
      return RKNN_NPU_CORE_ALL;
    }
    throw std::runtime_error("Invalid NPU core: " + core_str + ". Valid options: auto, 0, 1, 2, 0_1, 0_1_2, all");
  }

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult {
    cxxopts::Options options{"signlang_eyes_signlang_manager",
                             "Expose handpose streaming and sign language prototype database management over BLE"};

    options.add_options()("i,input-service", "Input handpose iceoryx2 service name", cxxopts::value<std::string>())(
        "signlang-result-service", "signlang_det result publish-subscribe service name", cxxopts::value<std::string>())(
        "signlang-control-service", "signlang_det prototype reload request-response service name",
        cxxopts::value<std::string>())("bluetooth-name", "BLE advertising local name",
                                       cxxopts::value<std::string>()->default_value(kDefaultBluetoothName))(
        "adapter-path", "BlueZ adapter object path", cxxopts::value<std::string>()->default_value(kDefaultAdapterPath))(
        "m,model", "RKNN BiLSTM encoder model path", cxxopts::value<std::string>()->default_value(kDefaultModelPath))(
        "prototypes", "Gesture prototype SQLite database file",
        cxxopts::value<std::string>()->default_value(kDefaultPrototypesPath))(
        "min-confidence", "Minimum keypoint confidence for uploaded gesture samples",
        cxxopts::value<float>()->default_value(std::to_string(kDefaultMinConfidence)))(
        "motion-weight", "Motion feature weight used when encoding uploaded samples",
        cxxopts::value<float>()->default_value(std::to_string(kDefaultMotionWeight)))(
        "upload-window-overlap", "Overlap ratio when long uploads are split into multiple samples",
        cxxopts::value<float>()->default_value(std::to_string(kDefaultUploadWindowOverlap)))(
        "subscriber-buffer", "iceoryx2 subscriber queue size",
        cxxopts::value<std::uint64_t>()->default_value(std::to_string(kDefaultSubscriberBufferSize)))(
        "stream-fps", "Maximum BLE handpose streaming frame rate",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultStreamFps)))(
        "max-notify-payload", "Maximum BLE notification payload chunk size",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultMaxNotifyPayload)))(
        "max-upload-bytes", "Maximum BLE gesture upload size in bytes",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultMaxUploadBytes)))(
        "npu-core", "NPU core selection: auto,0,1,2,0_1,0_1_2,all",
        cxxopts::value<std::string>()->default_value("auto"))(
        "enable-streaming-by-default", "Start handpose streaming as soon as a BLE client subscribes")("h,help",
                                                                                                      "Print usage");
    signlang::logging::add_cli_options(options);

    const auto parsed_options = options.parse(argc, argv);
    if (parsed_options.count("help") != 0) {
      return ProgramUsage{.text = options.help()};
    }

    if (parsed_options.count("input-service") == 0 || parsed_options.count("signlang-result-service") == 0 ||
        parsed_options.count("signlang-control-service") == 0) {
      throw std::runtime_error(
          "Options --input-service, --signlang-result-service, and --signlang-control-service are required.\n\n" +
          options.help());
    }

    const auto min_confidence = parsed_options["min-confidence"].as<float>();
    if (min_confidence < 0.0F || min_confidence > 1.0F) {
      throw std::runtime_error("--min-confidence must be in [0.0, 1.0]");
    }

    const auto motion_weight = parsed_options["motion-weight"].as<float>();
    if (motion_weight < 0.0F || motion_weight > 1.0F) {
      throw std::runtime_error("--motion-weight must be in [0.0, 1.0]");
    }

    const auto upload_window_overlap = parsed_options["upload-window-overlap"].as<float>();
    if (upload_window_overlap < 0.0F || upload_window_overlap >= 1.0F) {
      throw std::runtime_error("--upload-window-overlap must be in [0.0, 1.0)");
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
        .signlang_control_service_name = parsed_options["signlang-control-service"].as<std::string>(),
        .bluetooth_name = parsed_options["bluetooth-name"].as<std::string>(),
        .adapter_path = parsed_options["adapter-path"].as<std::string>(),
        .model_path = parsed_options["model"].as<std::string>(),
        .prototypes_path = parsed_options["prototypes"].as<std::string>(),
        .min_keypoint_confidence = min_confidence,
        .motion_weight = motion_weight,
        .upload_window_overlap = upload_window_overlap,
        .subscriber_buffer_size = subscriber_buffer_size,
        .stream_fps = stream_fps,
        .max_notify_payload = max_notify_payload,
        .max_upload_bytes = max_upload_bytes,
        .npu_core_mask = parse_npu_core_mask(parsed_options["npu-core"].as<std::string>()),
        .enable_streaming_by_default = parsed_options.count("enable-streaming-by-default") != 0,
        .logging = signlang::logging::parse_cli_options(parsed_options),
    };
  }

} // namespace signlang::signlang_manager
