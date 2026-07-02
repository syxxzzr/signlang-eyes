#ifndef SIGNLANG_EYES_DATAFLOW_DISPATCHER_PROGRAM_OPTIONS_HPP
#define SIGNLANG_EYES_DATAFLOW_DISPATCHER_PROGRAM_OPTIONS_HPP

#include "common/logging.hpp"

#include <cstdint>
#include <string>
#include <variant>

namespace signlang::dataflow_dispatcher {

  struct ProgramOptions {
    std::string state_event_service_name;
    std::string state_blackboard_service_name;
    std::string signlang_result_service_name;
    std::string speech_tts_service_name;
    std::string llm_client_service_name;
    std::string peripheral_display_service_name;
    std::uint64_t subscriber_buffer_size;
    std::uint64_t signlang_ai_window_ms;
    signlang::logging::Options logging;
  };

  struct ProgramUsage {
    std::string text;
  };

  using ProgramOptionsParseResult = std::variant<ProgramOptions, ProgramUsage>;

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult;

} // namespace signlang::dataflow_dispatcher

#endif // SIGNLANG_EYES_DATAFLOW_DISPATCHER_PROGRAM_OPTIONS_HPP
