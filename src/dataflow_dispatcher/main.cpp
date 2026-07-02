#include "common/runtime.hpp"
#include "core.hpp"
#include "iceoryx_gateway.hpp"
#include "program_options.hpp"
#include "spdlog/spdlog.h"

#include <chrono>
#include <optional>
#include <thread>

namespace {

  constexpr std::uint64_t kActiveWaitMs = 5;
  constexpr std::uint64_t kIdleStateWaitMs = 50;

  void apply_upstream_for_state(signlang::state_machine::AppState state,
                                const signlang::dataflow_dispatcher::ProgramOptions& options,
                                std::optional<signlang::dataflow_dispatcher::IpcSignlangResultSubscriber>&
                                    signlang_subscriber,
                                signlang::dataflow_dispatcher::RequiredUpstream& active_upstream) {
    const auto required_upstream = signlang::dataflow_dispatcher::required_upstream_for_state(state);
    if (required_upstream == active_upstream) {
      return;
    }

    signlang_subscriber.reset();
    active_upstream = signlang::dataflow_dispatcher::RequiredUpstream::None;

    if (required_upstream == signlang::dataflow_dispatcher::RequiredUpstream::SignlangResult) {
      signlang_subscriber.emplace(options.signlang_result_service_name, options.subscriber_buffer_size);
      active_upstream = required_upstream;
      spdlog::info("Dataflow dispatcher subscribed to signlang_det results for state {}",
                   signlang::state_machine::app_state_name(state));
      return;
    }

    spdlog::info("Dataflow dispatcher has no upstream subscription for state {}",
                 signlang::state_machine::app_state_name(state));
  }

} // namespace

auto main(int argc, char** argv) -> int {
  using signlang::dataflow_dispatcher::IpcSpeechTtsClient;
  using signlang::dataflow_dispatcher::IpcStateSubscriber;
  using signlang::dataflow_dispatcher::IpcSignlangResultSubscriber;
  using signlang::dataflow_dispatcher::RequiredUpstream;
  using signlang::dataflow_dispatcher::parse_program_options;
  using signlang::dataflow_dispatcher::tts_text_from_signlang_result;

  return signlang::runtime::run_module(argc, argv, parse_program_options, [](const auto& options) {
    spdlog::info("Starting dataflow dispatcher");
    spdlog::info("State event service: {}", options.state_event_service_name);
    spdlog::info("State blackboard service: {}", options.state_blackboard_service_name);
    spdlog::info("signlang_det result service: {}", options.signlang_result_service_name);
    spdlog::info("speech_tts service: {}", options.speech_tts_service_name);

    auto state_subscriber = IpcStateSubscriber{options.state_event_service_name, options.state_blackboard_service_name};
    auto tts_client = IpcSpeechTtsClient{options.speech_tts_service_name};
    auto signlang_subscriber = std::optional<IpcSignlangResultSubscriber>{};
    auto active_upstream = RequiredUpstream::None;
    auto current_state = state_subscriber.current_state();

    apply_upstream_for_state(current_state, options, signlang_subscriber, active_upstream);

    while (!signlang::runtime::shutdown_requested()) {
      if (state_subscriber.poll_state_change()) {
        current_state = state_subscriber.current_state();
        apply_upstream_for_state(current_state, options, signlang_subscriber, active_upstream);
      }

      if (!signlang_subscriber.has_value()) {
        if (state_subscriber.wait_for_state_change(kIdleStateWaitMs)) {
          current_state = state_subscriber.current_state();
          apply_upstream_for_state(current_state, options, signlang_subscriber, active_upstream);
        }
        continue;
      }

      if (!signlang_subscriber->wait_for_work(kActiveWaitMs)) {
        continue;
      }

      signlang_subscriber->receive_latest([&](const auto& result) {
        const auto text = tts_text_from_signlang_result(result);
        if (!text.has_value()) {
          return;
        }

        const auto response = tts_client.send_text(text.value());
        if (response.status != signlang::speech_tts::SpeechTtsStatus::Ok) {
          spdlog::warn("speech_tts rejected signlang result '{}': {}", text.value(), response.message.data());
        } else {
          spdlog::info("Forwarded signlang result to speech_tts: {}", text.value());
        }
      });
    }

    signlang_subscriber.reset();
    return 0;
  });
}
