#include "program_options.hpp"

#include <cassert>
#include <iterator>
#include <stdexcept>
#include <string>
#include <variant>

namespace {

  using signlang::state_machine::parse_program_options;
  using signlang::state_machine::ProgramOptions;

  void parses_state_control_service_name() {
    const char* argv[] = {
        "state_machine",
        "--state-event-service",
        "app/state/event",
        "--state-blackboard-service",
        "app/state/blackboard",
        "--state-control-service",
        "app/state/control",
    };
    auto result = parse_program_options(static_cast<int>(std::size(argv)), const_cast<char**>(argv));
    const auto& options = std::get<ProgramOptions>(result);
    assert(options.state_event_service_name == "app/state/event");
    assert(options.state_blackboard_service_name == "app/state/blackboard");
    assert(options.state_control_service_name == "app/state/control");
  }

  void requires_state_control_service_name() {
    const char* argv[] = {
        "state_machine",
        "--state-event-service",
        "app/state/event",
        "--state-blackboard-service",
        "app/state/blackboard",
    };

    bool threw = false;
    try {
      (void)parse_program_options(static_cast<int>(std::size(argv)), const_cast<char**>(argv));
    } catch (const std::runtime_error& error) {
      threw = true;
      assert(std::string{error.what()}.find("--state-control-service") != std::string::npos);
    }
    assert(threw);
  }

} // namespace

auto main() -> int {
  parses_state_control_service_name();
  requires_state_control_service_name();
  return 0;
}
