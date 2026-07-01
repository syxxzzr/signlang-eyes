#include "state_control.hpp"

#include <array>
#include <chrono>

namespace signlang::state_machine {
  namespace {

    constexpr std::array<AppState, 4> kBaseStates{
        AppState::Normal,
        AppState::Asr,
        AppState::SignLanguageChat,
        AppState::SignLanguageAi,
    };

  } // namespace

  StateController::StateController(AppState initial_base_state) :
      base_state_{is_basic_app_state(initial_base_state) ? initial_base_state : AppState::Normal},
      published_state_{base_state_} {}

  auto StateController::current_base_state() const -> AppState { return base_state_; }

  auto StateController::current_published_state() const -> AppState { return published_state_; }

  auto StateController::current_special_state_value() const -> std::int32_t {
    if (!special_state_.has_value()) {
      return kNoSpecialState;
    }

    return static_cast<std::int32_t>(special_state_.value());
  }

  auto StateController::handle_request(const StateControlRequest& request, Clock::time_point now)
      -> StateControlResponse {
    switch (request.command) {
    case StateControlCommand::NextBase:
      if (special_state_.has_value()) {
        return make_response(false, StateControlErrorCode::IgnoredDuringSpecialState);
      }
      base_state_ = next_base_state();
      published_state_ = base_state_;
      return make_response(true, StateControlErrorCode::None);

    case StateControlCommand::SetBase:
      if (!is_basic_app_state(request.target_state)) {
        return make_response(false, StateControlErrorCode::InvalidTargetState);
      }
      if (special_state_.has_value()) {
        return make_response(false, StateControlErrorCode::IgnoredDuringSpecialState);
      }
      base_state_ = request.target_state;
      published_state_ = base_state_;
      return make_response(true, StateControlErrorCode::None);

    case StateControlCommand::EnterSpecial:
      if (request.target_state != AppState::DangerousSound) {
        return make_response(false, StateControlErrorCode::InvalidTargetState);
      }
      special_state_ = request.target_state;
      published_state_ = request.target_state;
      special_state_expires_at_ =
          now + std::chrono::milliseconds(request.timeout_ms == 0 ? kDefaultSpecialStateTimeoutMs : request.timeout_ms);
      return make_response(true, StateControlErrorCode::None);
    }

    return make_response(false, StateControlErrorCode::InvalidCommand);
  }

  auto StateController::expire_special_state(Clock::time_point now) -> bool {
    if (!special_state_.has_value() || now < special_state_expires_at_) {
      return false;
    }

    special_state_.reset();
    published_state_ = base_state_;
    return true;
  }

  auto StateController::make_response(bool accepted, StateControlErrorCode error_code) const -> StateControlResponse {
    return StateControlResponse{accepted, base_state_, current_special_state_value(), error_code};
  }

  auto StateController::next_base_state() const -> AppState {
    for (std::size_t index = 0; index < kBaseStates.size(); ++index) {
      if (kBaseStates[index] == base_state_) {
        return kBaseStates[(index + 1) % kBaseStates.size()];
      }
    }

    return AppState::Normal;
  }

} // namespace signlang::state_machine
