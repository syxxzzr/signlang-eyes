#ifndef SIGNLANG_EYES_PERIPHERAL_SERVICE_PROGRAM_OPTIONS_HPP
#define SIGNLANG_EYES_PERIPHERAL_SERVICE_PROGRAM_OPTIONS_HPP

#include "common/logging.hpp"
#include "scrolling_display.hpp"
#include "serial_transport.hpp"

#include <string>
#include <variant>

namespace signlang::peripheral_service {

  struct ProgramUsage {
    std::string text;
  };

  struct ProgramOptions {
    SerialOptions serial;
    DisplayOptions display;
    std::string font_file;
    std::string display_service_name;
    std::string state_event_service_name;
    std::string state_blackboard_service_name;
    std::string state_control_service_name;
    std::string alert_event_service_name;
    signlang::logging::Options logging;
  };

  using ProgramOptionsParseResult = std::variant<ProgramOptions, ProgramUsage>;

  [[nodiscard]] auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult;

} // namespace signlang::peripheral_service

#endif // SIGNLANG_EYES_PERIPHERAL_SERVICE_PROGRAM_OPTIONS_HPP
