#ifndef SIGNLANG_EYES_SPEECH_ASR_PROGRAM_OPTIONS_HPP
#define SIGNLANG_EYES_SPEECH_ASR_PROGRAM_OPTIONS_HPP

#include "common/logging.hpp"
#include "rknn_api.h"
#include "speech_asr_result.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace signlang::speech_asr {

  struct ProgramOptions {
    std::string audio_service_name;
    std::string result_service_name;
    std::optional<std::string> state_event_service_name;
    std::optional<std::string> state_blackboard_service_name;
    std::string encoder_model_path;
    std::string decoder_model_path;
    std::string vocab_en_path;
    std::string vocab_zh_path;
    std::string mel_filters_path;
    std::uint32_t window_ms;
    double overlap_ratio;
    AsrLanguage language;
    std::uint32_t max_decode_steps;
    std::uint64_t subscriber_buffer_size;
    rknn_core_mask encoder_npu_core_mask;
    rknn_core_mask decoder_npu_core_mask;
    std::uint32_t rknn_priority_flag;
    signlang::logging::Options logging;
  };

  struct ProgramUsage {
    std::string text;
  };

  using ProgramOptionsParseResult = std::variant<ProgramOptions, ProgramUsage>;

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult;

} // namespace signlang::speech_asr

#endif // SIGNLANG_EYES_SPEECH_ASR_PROGRAM_OPTIONS_HPP
