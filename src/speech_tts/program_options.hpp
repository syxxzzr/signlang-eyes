#ifndef SIGNLANG_EYES_SPEECH_TTS_PROGRAM_OPTIONS_HPP
#define SIGNLANG_EYES_SPEECH_TTS_PROGRAM_OPTIONS_HPP

#include "common/cpu_affinity.hpp"
#include "common/logging.hpp"

#include "rknn_api.h"

#include <cstdint>
#include <string>
#include <variant>

namespace signlang::speech_tts {

  struct ProgramOptions {
    std::string service_name;
    std::string audio_device_name;
    std::string encoder_model_path;
    std::string decoder_model_path;
    std::string config_path;
    std::string pinyin_dictionary_path;
    rknn_core_mask decoder_npu_core_mask;
    std::uint32_t rknn_priority_flag;
    signlang::logging::Options logging;
    signlang::runtime::CpuAffinityOptions cpu_affinity;
  };

  struct ProgramUsage {
    std::string text;
  };

  using ProgramOptionsParseResult = std::variant<ProgramOptions, ProgramUsage>;

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult;

} // namespace signlang::speech_tts

#endif // SIGNLANG_EYES_SPEECH_TTS_PROGRAM_OPTIONS_HPP
