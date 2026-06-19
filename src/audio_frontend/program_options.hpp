#ifndef SIGNLANG_EYES_AUDIO_FRONTEND_PROGRAM_OPTIONS_HPP
#define SIGNLANG_EYES_AUDIO_FRONTEND_PROGRAM_OPTIONS_HPP

#include "audio_format.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace signlang::audio_frontend {

  struct ProgramOptions {
    std::string audio_device_name;
    std::string service_name;
    std::optional<std::string> localization_blackboard_service_name;
    float localization_tdoa_weight;
    float localization_rms_weight;
    std::uint32_t publish_period_ms;
    bool enable_denoise;
    AudioFormatRequest capture_format;
    AudioFormatRequest publish_format;
  };

  struct ProgramUsage {
    std::string text;
  };

  using ProgramOptionsParseResult = std::variant<ProgramOptions, ProgramUsage>;

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult;

} // namespace signlang::audio_frontend

#endif // SIGNLANG_EYES_AUDIO_FRONTEND_PROGRAM_OPTIONS_HPP
