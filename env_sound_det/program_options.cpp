#include "program_options.hpp"

#include "env_sound_result.hpp"

#include "cxxopts.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>

namespace signlang::env_sound_det {
  namespace {

    constexpr auto kDefaultModelPath = "models/yamnet/yamnet_3s.rknn";
    constexpr auto kDefaultClassMapPath = "models/yamnet/yamnet_class_map.txt";
    constexpr std::uint32_t kMinWindowMs = 100;
    constexpr std::uint32_t kMaxWindowMs = 60000;
    constexpr std::uint32_t kDefaultPollPeriodMs = 2;
    constexpr std::uint64_t kDefaultSubscriberBufferSize = 2;

    auto parse_npu_core_mask(const std::string& value) -> rknn_core_mask {
      if (value == "auto") {
        return RKNN_NPU_CORE_AUTO;
      }
      if (value == "all") {
        return RKNN_NPU_CORE_ALL;
      }
      if (value == "0") {
        return RKNN_NPU_CORE_0;
      }
      if (value == "1") {
        return RKNN_NPU_CORE_1;
      }
      if (value == "2") {
        return RKNN_NPU_CORE_2;
      }
      if (value == "0_1") {
        return RKNN_NPU_CORE_0_1;
      }
      if (value == "0_1_2") {
        return RKNN_NPU_CORE_0_1_2;
      }

      throw std::runtime_error("--npu-core must be one of auto, all, 0, 1, 2, 0_1, 0_1_2");
    }

    auto parse_rknn_priority_flag(const std::string& value) -> std::uint32_t {
      if (value == "high") {
        return RKNN_FLAG_PRIOR_HIGH;
      }
      if (value == "medium") {
        return RKNN_FLAG_PRIOR_MEDIUM;
      }
      if (value == "low") {
        return RKNN_FLAG_PRIOR_LOW;
      }

      throw std::runtime_error("--rknn-priority must be one of high, medium, low");
    }

  } // namespace

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult {
    cxxopts::Options options{
        "signlang_eyes_edgeai_env_sound_det",
        "Detect environmental sounds from an iceoryx2 audio stream with RKNN YAMNet and publish the result."};

    options.add_options()("i,input-service", "iceoryx2 audio input publish-subscribe service name",
                          cxxopts::value<std::string>())(
        "o,output-service", "iceoryx2 detection result publish-subscribe service name",
        cxxopts::value<std::string>())("m,model", "YAMNet RKNN model path",
                                       cxxopts::value<std::string>()->default_value(kDefaultModelPath))(
        "class-map", "YAMNet class map path",
        cxxopts::value<std::string>()->default_value(kDefaultClassMapPath))(
        "w,window-ms", "Detection window length in milliseconds",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultWindowMs)))(
        "overlap", "Overlap ratio between adjacent detection windows",
        cxxopts::value<double>()->default_value(std::to_string(kDefaultOverlapRatio)))(
        "top-k", "Number of top classes to publish",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kMaxTopClassCount)))(
        "poll-ms", "Subscriber polling sleep in milliseconds when no audio sample is ready",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultPollPeriodMs)))(
        "subscriber-buffer", "iceoryx2 subscriber queue size",
        cxxopts::value<std::uint64_t>()->default_value(std::to_string(kDefaultSubscriberBufferSize)))(
        "npu-core", "RK3588 NPU core mask: auto, all, 0, 1, 2, 0_1, 0_1_2",
        cxxopts::value<std::string>()->default_value("auto"))(
        "rknn-priority", "RKNN context priority: high, medium, low",
        cxxopts::value<std::string>()->default_value("medium"))("h,help", "Print usage");

    const auto parsed_options = options.parse(argc, argv);
    if (parsed_options.count("help") != 0) {
      return ProgramUsage{.text = options.help()};
    }

    if (parsed_options.count("input-service") == 0 || parsed_options.count("output-service") == 0) {
      throw std::runtime_error("Both --input-service and --output-service are required.\n\n" + options.help());
    }

    const auto window_ms = parsed_options["window-ms"].as<std::uint32_t>();
    if (window_ms < kMinWindowMs || window_ms > kMaxWindowMs) {
      throw std::runtime_error("--window-ms must be between " + std::to_string(kMinWindowMs) + " and " +
                               std::to_string(kMaxWindowMs));
    }

    const auto overlap_ratio = parsed_options["overlap"].as<double>();
    if (overlap_ratio < 0.0 || overlap_ratio >= 1.0) {
      throw std::runtime_error("--overlap must be in the range [0.0, 1.0)");
    }

    const auto top_k = parsed_options["top-k"].as<std::uint32_t>();
    if (top_k == 0 || top_k > kMaxTopClassCount) {
      throw std::runtime_error("--top-k must be between 1 and " + std::to_string(kMaxTopClassCount));
    }

    const auto poll_period_ms = parsed_options["poll-ms"].as<std::uint32_t>();
    if (poll_period_ms == 0 || poll_period_ms > 100) {
      throw std::runtime_error("--poll-ms must be between 1 and 100");
    }

    const auto subscriber_buffer_size = parsed_options["subscriber-buffer"].as<std::uint64_t>();
    if (subscriber_buffer_size == 0) {
      throw std::runtime_error("--subscriber-buffer must be greater than 0");
    }

    return ProgramOptionsParseResult{ProgramOptions{
        .audio_service_name = parsed_options["input-service"].as<std::string>(),
        .result_service_name = parsed_options["output-service"].as<std::string>(),
        .model_path = parsed_options["model"].as<std::string>(),
        .class_map_path = parsed_options["class-map"].as<std::string>(),
        .window_ms = window_ms,
        .overlap_ratio = overlap_ratio,
        .top_k = top_k,
        .poll_period_ms = poll_period_ms,
        .subscriber_buffer_size = subscriber_buffer_size,
        .npu_core_mask = parse_npu_core_mask(parsed_options["npu-core"].as<std::string>()),
        .rknn_priority_flag = parse_rknn_priority_flag(parsed_options["rknn-priority"].as<std::string>()),
    }};
  }

} // namespace signlang::env_sound_det
