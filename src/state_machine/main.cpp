#include "app_state.hpp"
#include "common/runtime.hpp"
#include "iceoryx_gateway.hpp"
#include "program_options.hpp"
#include "spdlog/spdlog.h"
#include "state_control.hpp"

#include <chrono>
#include <thread>

auto main(int argc, char** argv) -> int {
  using signlang::state_machine::app_state_name;
  using signlang::state_machine::IpcStateControlServer;
  using signlang::state_machine::IpcStatePublisher;
  using signlang::state_machine::parse_program_options;
  using signlang::state_machine::StateController;

  return signlang::runtime::run_module(argc, argv, parse_program_options, [&](const auto& options) {
    spdlog::info("Starting state machine");
    spdlog::info("State event service: {}", options.state_event_service_name);
    spdlog::info("State blackboard service: {}", options.state_blackboard_service_name);
    spdlog::info("State control service: {}", options.state_control_service_name);
    spdlog::info("Initial app state: {}", app_state_name(options.initial_state));

    StateController state_controller{options.initial_state};
    IpcStatePublisher state_publisher{options.state_event_service_name, options.state_blackboard_service_name,
                                      state_controller.current_published_state()};
    IpcStateControlServer state_control_server{options.state_control_service_name};

    spdlog::info("Published initial app state: {}", app_state_name(state_publisher.current_state()));

    while (!signlang::runtime::shutdown_requested()) {
      const auto now = StateController::Clock::now();
      if (state_controller.expire_special_state(now)) {
        spdlog::info("Special state expired, returning to normal");
        state_publisher.set_state(state_controller.current_published_state());
      }

      state_control_server.process_pending_requests(state_controller, state_publisher, now);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    spdlog::info("State machine stopped with current state {}", app_state_name(state_publisher.current_state()));
    return 0;
  });
}
