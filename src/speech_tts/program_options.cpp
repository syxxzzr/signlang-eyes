#include "program_options.hpp"

#include "common/cpu_affinity_cli.hpp"
#include "common/logging_cli.hpp"
#include "cxxopts.hpp"

#include <stdexcept>
#include <string>

namespace signlang::speech_tts {
  namespace {

    constexpr auto kDefaultEncoderModelPath = "models/piper/zh_CN-chaowen-medium.encoder.onnx";
    constexpr auto kDefaultDecoderModelPath = "models/piper/zh_CN-chaowen-medium.decoder.rknn";
    constexpr auto kDefaultConfigPath = "models/piper/zh_CN-chaowen-medium.json";
    constexpr auto kDefaultPinyinDictionaryPath = SIGNLANG_CPP_PINYIN_DICT_DIR;

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
    cxxopts::Options options{"signlang_eyes_speech_tts",
                             "Speak text requests received from an iceoryx2 request-response service with Piper."};

    options.add_options()("s,service", "iceoryx2 speech TTS request-response service name",
                          cxxopts::value<std::string>())(
        "d,device", "ALSA playback device name", cxxopts::value<std::string>()->default_value("default"))(
        "encoder-model", "Piper encoder ONNX model path",
        cxxopts::value<std::string>()->default_value(kDefaultEncoderModelPath))(
        "decoder-model", "Piper decoder RKNN model path",
        cxxopts::value<std::string>()->default_value(kDefaultDecoderModelPath))(
        "config", "Piper model JSON config path", cxxopts::value<std::string>()->default_value(kDefaultConfigPath))(
        "pinyin-dict", "cpp-pinyin dictionary directory",
        cxxopts::value<std::string>()->default_value(kDefaultPinyinDictionaryPath))(
        "npu-core", "RK3588 NPU core mask for Piper decoder: auto, all, 0, 1, 2, 0_1, 0_1_2",
        cxxopts::value<std::string>()->default_value("auto"))(
        "rknn-priority", "RKNN context priority: high, medium, low",
        cxxopts::value<std::string>()->default_value("medium"))("h,help", "Print usage");
    signlang::logging::add_cli_options(options);
    signlang::runtime::add_cpu_affinity_cli_options(options);

    const auto parsed_options = options.parse(argc, argv);
    if (parsed_options.count("help") != 0) {
      return ProgramUsage{options.help()};
    }

    if (parsed_options.count("service") == 0) {
      throw std::runtime_error("Option --service is required.\n\n" + options.help());
    }

    auto encoder_model_path = parsed_options["encoder-model"].as<std::string>();
    if (encoder_model_path.empty()) {
      throw std::runtime_error("--encoder-model must not be empty");
    }

    auto decoder_model_path = parsed_options["decoder-model"].as<std::string>();
    if (decoder_model_path.empty()) {
      throw std::runtime_error("--decoder-model must not be empty");
    }

    auto config_path = parsed_options["config"].as<std::string>();
    if (config_path.empty()) {
      throw std::runtime_error("--config must not be empty");
    }

    auto pinyin_dictionary_path = parsed_options["pinyin-dict"].as<std::string>();
    if (pinyin_dictionary_path.empty()) {
      throw std::runtime_error("--pinyin-dict must not be empty");
    }

    return ProgramOptionsParseResult{ProgramOptions{parsed_options["service"].as<std::string>(),
                                                    parsed_options["device"].as<std::string>(),
                                                    std::move(encoder_model_path),
                                                    std::move(decoder_model_path),
                                                    std::move(config_path),
                                                    std::move(pinyin_dictionary_path),
                                                    parse_npu_core_mask(parsed_options["npu-core"].as<std::string>()),
                                                    parse_rknn_priority_flag(
                                                        parsed_options["rknn-priority"].as<std::string>()),
                                                    signlang::logging::parse_cli_options(parsed_options),
                                                    signlang::runtime::parse_cpu_affinity_cli_options(parsed_options)}};
  }

} // namespace signlang::speech_tts
