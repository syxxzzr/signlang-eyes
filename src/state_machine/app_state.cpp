#include "app_state.hpp"

namespace signlang::state_machine {

  auto app_state_name(AppState state) -> const char* {
    switch (state) {
    case AppState::Normal:
      return "normal";
    case AppState::Asr:
      return "asr";
    case AppState::SignLanguageChat:
      return "sign_language_chat";
    case AppState::SignLanguageAi:
      return "sign_language_ai";
    }

    return "unknown";
  }

} // namespace signlang::state_machine
