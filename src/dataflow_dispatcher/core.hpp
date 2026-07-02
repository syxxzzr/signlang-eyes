#ifndef SIGNLANG_EYES_DATAFLOW_DISPATCHER_CORE_HPP
#define SIGNLANG_EYES_DATAFLOW_DISPATCHER_CORE_HPP

#include "signlang_det/signlang_result.hpp"
#include "state_machine/app_state.hpp"

#include <optional>
#include <string>

namespace signlang::dataflow_dispatcher {

  enum class RequiredUpstream {
    None,
    SignlangResult,
  };

  [[nodiscard]] auto required_upstream_for_state(signlang::state_machine::AppState state) -> RequiredUpstream;
  [[nodiscard]] auto tts_text_from_signlang_result(const signlang::signlang_det::SignlangResult& result)
      -> std::optional<std::string>;

} // namespace signlang::dataflow_dispatcher

#endif // SIGNLANG_EYES_DATAFLOW_DISPATCHER_CORE_HPP
