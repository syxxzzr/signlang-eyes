#ifndef SIGNLANG_EYES_STATE_MACHINE_PROGRAM_OPTIONS_HPP
#define SIGNLANG_EYES_STATE_MACHINE_PROGRAM_OPTIONS_HPP

#include "common/logging.hpp"

#include <string>
#include <variant>

namespace signlang::state_machine {

  struct ProgramOptions {
    std::string state_event_service_name;
    std::string state_blackboard_service_name;
    std::string state_control_service_name;
    signlang::logging::Options logging;
  };

  struct ProgramUsage {
    std::string text;
  };

  using ProgramOptionsParseResult = std::variant<ProgramOptions, ProgramUsage>;

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult;

} // namespace signlang::state_machine

#endif // SIGNLANG_EYES_STATE_MACHINE_PROGRAM_OPTIONS_HPP
