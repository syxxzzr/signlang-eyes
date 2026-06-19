#ifndef SIGNLANG_EYES_STATE_MACHINE_STATE_CONTROL_HPP
#define SIGNLANG_EYES_STATE_MACHINE_STATE_CONTROL_HPP

#include "app_state.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace signlang::state_machine {

  constexpr std::uint32_t kDefaultSpecialStateTimeoutMs = 15000;
  constexpr std::int32_t kNoSpecialState = -1;

  enum class StateControlCommand : std::uint32_t {
    NextBase = 0,
    SetBase = 1,
    EnterSpecial = 2,
  };

  enum class StateControlErrorCode : std::uint32_t {
    None = 0,
    InvalidTargetState = 1,
    IgnoredDuringSpecialState = 2,
    InvalidCommand = 3,
  };

  struct StateControlRequest {
    static constexpr const char* IOX2_TYPE_NAME = "signlang_state_control_request";

    StateControlCommand command;
    AppState target_state;
    std::uint32_t timeout_ms;
  };

  struct StateControlResponse {
    static constexpr const char* IOX2_TYPE_NAME = "signlang_state_control_response";

    bool accepted;
    AppState current_base_state;
    std::int32_t current_special_state;
    StateControlErrorCode error_code;
  };

  static_assert(std::is_trivially_copyable_v<StateControlRequest>);
  static_assert(std::is_trivially_copyable_v<StateControlResponse>);

  class StateController {
  public:
    using Clock = std::chrono::steady_clock;

    explicit StateController(AppState initial_base_state);

    auto current_base_state() const -> AppState;
    auto current_published_state() const -> AppState;
    auto current_special_state_value() const -> std::int32_t;

    auto handle_request(const StateControlRequest& request, Clock::time_point now) -> StateControlResponse;
    auto expire_special_state(Clock::time_point now) -> bool;

  private:
    auto make_response(bool accepted, StateControlErrorCode error_code) const -> StateControlResponse;
    auto next_base_state() const -> AppState;

    AppState base_state_;
    AppState published_state_;
    std::optional<AppState> special_state_;
    Clock::time_point special_state_expires_at_;
  };

} // namespace signlang::state_machine

#endif // SIGNLANG_EYES_STATE_MACHINE_STATE_CONTROL_HPP
