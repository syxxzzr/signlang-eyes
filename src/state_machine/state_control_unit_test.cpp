#include "state_control.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>

namespace {

  using signlang::state_machine::AppState;
  using signlang::state_machine::kDefaultSpecialStateTimeoutMs;
  using signlang::state_machine::kNoSpecialState;
  using signlang::state_machine::StateControlCommand;
  using signlang::state_machine::StateControlErrorCode;
  using signlang::state_machine::StateControlRequest;
  using signlang::state_machine::StateController;

  auto request(StateControlCommand command, AppState target_state = AppState::Normal,
               std::uint32_t timeout_ms = 0) -> StateControlRequest {
    return StateControlRequest{
        .command = command,
        .target_state = target_state,
        .timeout_ms = timeout_ms,
    };
  }

  void next_base_cycles_through_base_states() {
    auto controller = StateController{AppState::Normal};
    const auto now = StateController::Clock::time_point{};

    auto response = controller.handle_request(request(StateControlCommand::NextBase), now);
    assert(response.accepted);
    assert(response.current_base_state == AppState::Asr);
    assert(response.current_special_state == kNoSpecialState);
    assert(controller.current_published_state() == AppState::Asr);

    response = controller.handle_request(request(StateControlCommand::NextBase), now);
    assert(response.current_base_state == AppState::SignLanguageChat);

    response = controller.handle_request(request(StateControlCommand::NextBase), now);
    assert(response.current_base_state == AppState::SignLanguageAi);

    response = controller.handle_request(request(StateControlCommand::NextBase), now);
    assert(response.current_base_state == AppState::Normal);
  }

  void set_base_rejects_special_targets() {
    auto controller = StateController{AppState::Normal};
    const auto now = StateController::Clock::time_point{};

    const auto response =
        controller.handle_request(request(StateControlCommand::SetBase, AppState::DangerousSound), now);
    assert(!response.accepted);
    assert(response.error_code == StateControlErrorCode::InvalidTargetState);
    assert(response.current_base_state == AppState::Normal);
    assert(controller.current_published_state() == AppState::Normal);
  }

  void unknown_state_is_not_basic_or_special() {
    auto controller = StateController{AppState::Normal};
    const auto now = StateController::Clock::time_point{};
    const auto unknown_state = static_cast<AppState>(999);

    auto response = controller.handle_request(request(StateControlCommand::SetBase, unknown_state), now);
    assert(!response.accepted);
    assert(response.error_code == StateControlErrorCode::InvalidTargetState);
    assert(controller.current_published_state() == AppState::Normal);

    response = controller.handle_request(request(StateControlCommand::EnterSpecial, unknown_state), now);
    assert(!response.accepted);
    assert(response.error_code == StateControlErrorCode::InvalidTargetState);
    assert(controller.current_published_state() == AppState::Normal);
  }

  void special_state_uses_default_timeout_and_returns_to_previous_base() {
    auto controller = StateController{AppState::Asr};
    const auto now = StateController::Clock::time_point{};

    const auto response =
        controller.handle_request(request(StateControlCommand::EnterSpecial, AppState::DangerousSound), now);
    assert(response.accepted);
    assert(response.current_base_state == AppState::Asr);
    assert(response.current_special_state == static_cast<std::int32_t>(AppState::DangerousSound));
    assert(controller.current_published_state() == AppState::DangerousSound);

    assert(!controller.expire_special_state(now + std::chrono::milliseconds(kDefaultSpecialStateTimeoutMs - 1)));
    assert(controller.current_published_state() == AppState::DangerousSound);

    assert(controller.expire_special_state(now + std::chrono::milliseconds(kDefaultSpecialStateTimeoutMs)));
    assert(controller.current_published_state() == AppState::Asr);
    assert(controller.current_special_state_value() == kNoSpecialState);
  }

  void base_changes_are_ignored_during_special_state() {
    auto controller = StateController{AppState::SignLanguageChat};
    const auto now = StateController::Clock::time_point{};

    controller.handle_request(request(StateControlCommand::EnterSpecial, AppState::DangerousSound, 1000), now);
    const auto response =
        controller.handle_request(request(StateControlCommand::SetBase, AppState::Normal), now);
    assert(!response.accepted);
    assert(response.error_code == StateControlErrorCode::IgnoredDuringSpecialState);
    assert(response.current_base_state == AppState::SignLanguageChat);
    assert(response.current_special_state == static_cast<std::int32_t>(AppState::DangerousSound));
    assert(controller.current_published_state() == AppState::DangerousSound);
  }

  void new_special_state_replaces_existing_special_timer() {
    auto controller = StateController{AppState::SignLanguageAi};
    const auto now = StateController::Clock::time_point{};

    controller.handle_request(request(StateControlCommand::EnterSpecial, AppState::DangerousSound, 100), now);
    controller.handle_request(
        request(StateControlCommand::EnterSpecial, AppState::DangerousSound, 500), now + std::chrono::milliseconds(50));

    assert(!controller.expire_special_state(now + std::chrono::milliseconds(149)));
    assert(controller.current_published_state() == AppState::DangerousSound);

    assert(controller.expire_special_state(now + std::chrono::milliseconds(550)));
    assert(controller.current_published_state() == AppState::SignLanguageAi);
  }

} // namespace

auto main() -> int {
  next_base_cycles_through_base_states();
  set_base_rejects_special_targets();
  unknown_state_is_not_basic_or_special();
  special_state_uses_default_timeout_and_returns_to_previous_base();
  base_changes_are_ignored_during_special_state();
  new_special_state_replaces_existing_special_timer();
  return 0;
}
