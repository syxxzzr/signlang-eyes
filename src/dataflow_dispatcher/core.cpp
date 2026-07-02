#include "core.hpp"

#include "common/fixed_string.hpp"

namespace signlang::dataflow_dispatcher {

  auto required_upstream_for_state(signlang::state_machine::AppState state) -> RequiredUpstream {
    switch (state) {
    case signlang::state_machine::AppState::SignLanguageChat:
    case signlang::state_machine::AppState::SignLanguageAi:
      return RequiredUpstream::SignlangResult;
    case signlang::state_machine::AppState::Asr:
      return RequiredUpstream::SpeechAsrResult;
    case signlang::state_machine::AppState::Normal:
    case signlang::state_machine::AppState::DangerousSound:
      return RequiredUpstream::None;
    }

    return RequiredUpstream::None;
  }

  auto tts_text_from_signlang_result(const signlang::signlang_det::SignlangResult& result)
      -> std::optional<std::string> {
    if (!result.recognized) {
      return std::nullopt;
    }

    auto text = signlang::common::fixed_string_to_string(result.gesture_name);
    if (text.empty()) {
      return std::nullopt;
    }

    return text;
  }

  auto tts_text_from_speech_asr_result(const signlang::speech_asr::SpeechAsrResult& result)
      -> std::optional<std::string> {
    auto text = signlang::common::fixed_string_to_string(result.transcript);
    if (text.empty()) {
      return std::nullopt;
    }

    return text;
  }

} // namespace signlang::dataflow_dispatcher
