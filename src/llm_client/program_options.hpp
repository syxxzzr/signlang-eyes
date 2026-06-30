#ifndef SIGNLANG_EYES_LLM_CLIENT_PROGRAM_OPTIONS_HPP
#define SIGNLANG_EYES_LLM_CLIENT_PROGRAM_OPTIONS_HPP

#include "common/logging.hpp"

#include <cstdint>
#include <string>
#include <variant>

namespace signlang::llm_client {

  struct ProgramOptions {
    std::string service_name;
    std::string base_url;
    std::string api_key;
    std::string model;
    std::string system_prompt_file;
    std::uint32_t request_timeout_ms;
    signlang::logging::Options logging;
  };

  struct ProgramUsage {
    std::string text;
  };

  using ProgramOptionsParseResult = std::variant<ProgramOptions, ProgramUsage>;

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult;

} // namespace signlang::llm_client

#endif // SIGNLANG_EYES_LLM_CLIENT_PROGRAM_OPTIONS_HPP
