#include "program_options.hpp"

#include "speech_asr_result.hpp"

#include "cxxopts.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>

namespace signlang::speech_asr {
  namespace {

    constexpr auto kDefaultEncoderModelPath = "models/whisper/whisper_encoder_base_15s.rknn";
    constexpr auto kDefaultDecoderModelPath = "models/whisper/whisper_decoder_base_15s.rknn";
    constexpr auto kDefaultVocabEnPath = "models/whisper/vocab_en.txt";
    constexpr auto kDefaultVocabZhPath = "models/whisper/vocab_zh.txt";
    constexpr auto kDefaultMelFiltersPath = "models/whisper/mel_80_filters.txt";
    constexpr std::uint32_t kMinWindowMs = 1000;
    constexpr std::uint32_t kMaxWindowMs = 60000;
    constexpr std::uint32_t kDefaultPollPeriodMs = 2;
    constexpr std::uint32_t kDefaultEnableRequestTimeoutMs = 50;
    constexpr std::uint32_t kDefaultMaxDecodeSteps = 96;
    constexpr std::uint64_t kDefaultSubscriberBufferSize = 4;

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

      throw std::runtime_error("--npu-core, --encoder-npu-core and --decoder-npu-core must be one of auto, all, 0, "
                               "1, 2, 0_1, 0_1_2");
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
        "signlang_eyes_edgeai_speech_asr",
        "Recognize speech from an iceoryx2 audio stream with RKNN Whisper and publish ASR results."};

    options.add_options()("i,input-service", "iceoryx2 audio input publish-subscribe service name",
                          cxxopts::value<std::string>())(
        "o,output-service", "iceoryx2 ASR result publish-subscribe service name",
        cxxopts::value<std::string>())(
        "enable-service", "iceoryx2 request-response service name for active ASR enable queries",
        cxxopts::value<std::string>())("encoder-model", "Whisper encoder RKNN model path",
                                       cxxopts::value<std::string>()->default_value(kDefaultEncoderModelPath))(
        "decoder-model", "Whisper decoder RKNN model path",
        cxxopts::value<std::string>()->default_value(kDefaultDecoderModelPath))(
        "vocab-en", "English vocabulary path", cxxopts::value<std::string>()->default_value(kDefaultVocabEnPath))(
        "vocab-zh", "Chinese vocabulary path", cxxopts::value<std::string>()->default_value(kDefaultVocabZhPath))(
        "mel-filters", "Whisper 80-bin mel filter path",
        cxxopts::value<std::string>()->default_value(kDefaultMelFiltersPath))(
        "w,window-ms", "ASR window length in milliseconds",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultWindowMs)))(
        "overlap", "Overlap ratio between adjacent ASR windows",
        cxxopts::value<double>()->default_value(std::to_string(kDefaultOverlapRatio)))(
        "poll-ms", "Subscriber polling sleep in milliseconds when no audio sample is ready",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultPollPeriodMs)))(
        "enable-timeout-ms", "Maximum time to wait for each enable request response",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultEnableRequestTimeoutMs)))(
        "max-decode-steps", "Maximum autoregressive decoder iterations per ASR window",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultMaxDecodeSteps)))(
        "subscriber-buffer", "iceoryx2 subscriber queue size",
        cxxopts::value<std::uint64_t>()->default_value(std::to_string(kDefaultSubscriberBufferSize)))(
        "npu-core", "Default RK3588 NPU core mask for both encoder and decoder: auto, all, 0, 1, 2, 0_1, 0_1_2",
        cxxopts::value<std::string>()->default_value("auto"))(
        "encoder-npu-core", "RK3588 NPU core mask for encoder; overrides --npu-core",
        cxxopts::value<std::string>())("decoder-npu-core", "RK3588 NPU core mask for decoder; overrides --npu-core",
                                       cxxopts::value<std::string>())(
        "rknn-priority", "RKNN context priority: high, medium, low",
        cxxopts::value<std::string>()->default_value("medium"))("h,help", "Print usage");

    const auto parsed_options = options.parse(argc, argv);
    if (parsed_options.count("help") != 0) {
      return ProgramUsage{.text = options.help()};
    }

    if (parsed_options.count("input-service") == 0 || parsed_options.count("output-service") == 0 ||
        parsed_options.count("enable-service") == 0) {
      throw std::runtime_error("Options --input-service, --output-service and --enable-service are required.\n\n" +
                               options.help());
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

    const auto poll_period_ms = parsed_options["poll-ms"].as<std::uint32_t>();
    if (poll_period_ms == 0 || poll_period_ms > 100) {
      throw std::runtime_error("--poll-ms must be between 1 and 100");
    }

    const auto enable_request_timeout_ms = parsed_options["enable-timeout-ms"].as<std::uint32_t>();
    if (enable_request_timeout_ms == 0 || enable_request_timeout_ms > 5000) {
      throw std::runtime_error("--enable-timeout-ms must be between 1 and 5000");
    }

    const auto max_decode_steps = parsed_options["max-decode-steps"].as<std::uint32_t>();
    if (max_decode_steps == 0 || max_decode_steps > 1000) {
      throw std::runtime_error("--max-decode-steps must be between 1 and 1000");
    }

    const auto subscriber_buffer_size = parsed_options["subscriber-buffer"].as<std::uint64_t>();
    if (subscriber_buffer_size == 0) {
      throw std::runtime_error("--subscriber-buffer must be greater than 0");
    }

    const auto default_npu_core = parsed_options["npu-core"].as<std::string>();
    const auto encoder_npu_core =
        parsed_options.count("encoder-npu-core") != 0 ? parsed_options["encoder-npu-core"].as<std::string>()
                                                      : default_npu_core;
    const auto decoder_npu_core =
        parsed_options.count("decoder-npu-core") != 0 ? parsed_options["decoder-npu-core"].as<std::string>()
                                                      : default_npu_core;

    return ProgramOptionsParseResult{ProgramOptions{
        .audio_service_name = parsed_options["input-service"].as<std::string>(),
        .result_service_name = parsed_options["output-service"].as<std::string>(),
        .enable_service_name = parsed_options["enable-service"].as<std::string>(),
        .encoder_model_path = parsed_options["encoder-model"].as<std::string>(),
        .decoder_model_path = parsed_options["decoder-model"].as<std::string>(),
        .vocab_en_path = parsed_options["vocab-en"].as<std::string>(),
        .vocab_zh_path = parsed_options["vocab-zh"].as<std::string>(),
        .mel_filters_path = parsed_options["mel-filters"].as<std::string>(),
        .window_ms = window_ms,
        .overlap_ratio = overlap_ratio,
        .poll_period_ms = poll_period_ms,
        .enable_request_timeout_ms = enable_request_timeout_ms,
        .max_decode_steps = max_decode_steps,
        .subscriber_buffer_size = subscriber_buffer_size,
        .encoder_npu_core_mask = parse_npu_core_mask(encoder_npu_core),
        .decoder_npu_core_mask = parse_npu_core_mask(decoder_npu_core),
        .rknn_priority_flag = parse_rknn_priority_flag(parsed_options["rknn-priority"].as<std::string>()),
    }};
  }

} // namespace signlang::speech_asr
