#include "app_state.hpp"
#include "common/logging.hpp"
#include "iceoryx_gateway.hpp"
#include "program_options.hpp"
#include "spdlog/spdlog.h"
#include "state_control.hpp"

#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <thread>
#include <variant>

namespace {

  volatile std::sig_atomic_t g_should_stop = 0;

  void handle_shutdown_signal(int /* signal_number */) { g_should_stop = 1; }

  void install_signal_handlers() {
    std::signal(SIGINT, handle_shutdown_signal);
    std::signal(SIGTERM, handle_shutdown_signal);
  }

} // namespace

auto main(int argc, char** argv) -> int {
  using signlang::state_machine::app_state_name;
  using signlang::state_machine::AppState;
  using signlang::state_machine::IpcStateControlServer;
  using signlang::state_machine::IpcStatePublisher;
  using signlang::state_machine::parse_program_options;
  using signlang::state_machine::ProgramOptions;
  using signlang::state_machine::ProgramUsage;
  using signlang::state_machine::StateController;

  signlang::logging::initialize();

  try {
    const auto parse_result = parse_program_options(argc, argv);
    if (const auto* usage = std::get_if<ProgramUsage>(&parse_result); usage != nullptr) {
      std::cout << usage->text << '\n';
      return 0;
    }

    const auto& options = std::get<ProgramOptions>(parse_result);
    signlang::logging::initialize(options.logging);
    install_signal_handlers();

    spdlog::info("Starting state machine");
    spdlog::info("State event service: {}", options.state_event_service_name);
    spdlog::info("State blackboard service: {}", options.state_blackboard_service_name);
    spdlog::info("State control service: {}", options.state_control_service_name);

    StateController state_controller{AppState::Normal};
    IpcStatePublisher state_publisher{options.state_event_service_name, options.state_blackboard_service_name,
                                      state_controller.current_published_state()};
    IpcStateControlServer state_control_server{options.state_control_service_name};

    spdlog::info("Published initial app state: {}", app_state_name(state_publisher.current_state()));

    while (g_should_stop == 0) {
      const auto now = StateController::Clock::now();
      if (state_controller.expire_special_state(now)) {
        spdlog::info("Special state expired, returning to normal");
        state_publisher.set_state(state_controller.current_published_state());
      }

      state_control_server.process_pending_requests(state_controller, state_publisher, now);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return 0;
  } catch (const std::exception& error) {
    spdlog::error("{}", error.what());
    return 1;
  }
}
