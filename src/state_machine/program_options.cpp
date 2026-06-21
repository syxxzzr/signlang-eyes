#include "program_options.hpp"

#include "common/logging_cli.hpp"
#include "cxxopts.hpp"

#include <stdexcept>
#include <string>

namespace signlang::state_machine {

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult {
    cxxopts::Options options{"signlang_eyes_state_machine",
                             "Publish the global application state through iceoryx2 blackboard and event services."};

    options.add_options()("state-event-service", "iceoryx2 event service name for app state change notifications",
                          cxxopts::value<std::string>())("state-blackboard-service",
                                                         "iceoryx2 blackboard service name for app state storage",
                                                         cxxopts::value<std::string>())(
        "state-control-service", "iceoryx2 request-response service name for app state control",
        cxxopts::value<std::string>())("h,help", "Print usage");
    signlang::logging::add_cli_options(options);

    const auto parsed_options = options.parse(argc, argv);
    if (parsed_options.count("help") != 0) {
      return ProgramUsage{.text = options.help()};
    }

    if (parsed_options.count("state-event-service") == 0 || parsed_options.count("state-blackboard-service") == 0 ||
        parsed_options.count("state-control-service") == 0) {
      throw std::runtime_error("Options --state-event-service, --state-blackboard-service, and "
                               "--state-control-service are required.\n\n" +
                               options.help());
    }

    return ProgramOptionsParseResult{ProgramOptions{
        .state_event_service_name = parsed_options["state-event-service"].as<std::string>(),
        .state_blackboard_service_name = parsed_options["state-blackboard-service"].as<std::string>(),
        .state_control_service_name = parsed_options["state-control-service"].as<std::string>(),
        .logging = signlang::logging::parse_cli_options(parsed_options),
    }};
  }

} // namespace signlang::state_machine
