#ifndef SIGNLANG_EYES_STATE_MACHINE_APP_STATE_HPP
#define SIGNLANG_EYES_STATE_MACHINE_APP_STATE_HPP

#include <cstdint>
#include <type_traits>

namespace signlang::state_machine {

  enum class AppState : std::uint32_t {
    Normal = 0,
    Asr = 1,
    SignLanguageChat = 2,
    SignLanguageAi = 3,
  };

  struct AppStateKey {
    static constexpr const char* IOX2_TYPE_NAME = "signlang_app_state_key";
    std::uint32_t id;

    auto operator==(const AppStateKey& other) const -> bool { return id == other.id; }
    auto operator!=(const AppStateKey& other) const -> bool { return id != other.id; }
  };

  static_assert(std::is_trivially_copyable_v<AppState>);
  static_assert(std::is_trivially_copyable_v<AppStateKey>);

  constexpr auto default_app_state_key() -> AppStateKey { return AppStateKey{.id = 0}; }
  auto app_state_name(AppState state) -> const char*;

} // namespace signlang::state_machine

#endif // SIGNLANG_EYES_STATE_MACHINE_APP_STATE_HPP
