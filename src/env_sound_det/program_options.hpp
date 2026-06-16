#ifndef SIGNLANG_EYES_EDGEAI_ENV_SOUND_DET_PROGRAM_OPTIONS_HPP
#define SIGNLANG_EYES_EDGEAI_ENV_SOUND_DET_PROGRAM_OPTIONS_HPP

#include "rknn_api.h"

#include <cstdint>
#include <string>
#include <variant>

namespace signlang::env_sound_det {

  struct ProgramOptions {
    std::string audio_service_name;
    std::string result_service_name;
    std::string model_path;
    std::string class_map_path;
    std::uint32_t window_ms;
    double overlap_ratio;
    std::uint32_t top_k;
    std::uint32_t poll_period_ms;
    std::uint64_t subscriber_buffer_size;
    rknn_core_mask npu_core_mask;
    std::uint32_t rknn_priority_flag;
  };

  struct ProgramUsage {
    std::string text;
  };

  using ProgramOptionsParseResult = std::variant<ProgramOptions, ProgramUsage>;

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult;

} // namespace signlang::env_sound_det

#endif // SIGNLANG_EYES_EDGEAI_ENV_SOUND_DET_PROGRAM_OPTIONS_HPP
