#ifndef SIGNLANG_EYES_VIDEO_FRONTEND_PROGRAM_OPTIONS_HPP
#define SIGNLANG_EYES_VIDEO_FRONTEND_PROGRAM_OPTIONS_HPP

#include "common/logging.hpp"
#include "video_format.hpp"

#include <cstdint>
#include <string>
#include <variant>

namespace signlang::video_frontend {

  struct ProgramOptions {
    std::string camera_device_name;
    std::string service_name;
    VideoFormatRequest capture_format;
    VideoFormatRequest output_format;
    std::uint32_t fps;
    signlang::logging::Options logging;
  };

  struct ProgramUsage {
    std::string text;
  };

  using ProgramOptionsParseResult = std::variant<ProgramOptions, ProgramUsage>;

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult;

} // namespace signlang::video_frontend

#endif // SIGNLANG_EYES_VIDEO_FRONTEND_PROGRAM_OPTIONS_HPP
