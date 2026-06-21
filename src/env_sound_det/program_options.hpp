#ifndef SIGNLANG_EYES_ENV_SOUND_DET_PROGRAM_OPTIONS_HPP
#define SIGNLANG_EYES_ENV_SOUND_DET_PROGRAM_OPTIONS_HPP

#include "common/logging.hpp"
#include "rknn_api.h"

#include <array>
#include <cstdint>
#include <string>
#include <variant>

namespace signlang::env_sound_det {

  constexpr std::uint32_t kYamnetSampleRateHz = 16000;
  constexpr std::uint32_t kDefaultWindowMs = 3000;
  constexpr double kDefaultOverlapRatio = 0.2;
  constexpr float kDefaultScoreThreshold = 0.3F;
  constexpr std::uint32_t kMaxClassLabelLength = 128;

  struct EnvSoundClassScore {
    std::uint32_t class_index;
    float score;
    std::array<char, kMaxClassLabelLength> label;
  };

  struct ProgramOptions {
    std::string audio_service_name;
    std::string state_control_service_name;
    std::string model_path;
    std::string class_map_path;
    std::uint32_t window_ms;
    double overlap_ratio;
    float score_threshold;
    std::uint64_t subscriber_buffer_size;
    rknn_core_mask npu_core_mask;
    std::uint32_t rknn_priority_flag;
    signlang::logging::Options logging;
  };

  struct ProgramUsage {
    std::string text;
  };

  using ProgramOptionsParseResult = std::variant<ProgramOptions, ProgramUsage>;

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult;

} // namespace signlang::env_sound_det

#endif // SIGNLANG_EYES_ENV_SOUND_DET_PROGRAM_OPTIONS_HPP
