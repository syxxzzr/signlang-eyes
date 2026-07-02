#include "common/runtime.hpp"
#include "common/fixed_string.hpp"
#include "core.hpp"
#include "iceoryx_gateway.hpp"
#include "program_options.hpp"
#include "spdlog/spdlog.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>

namespace {

  constexpr std::uint64_t kActiveWaitMs = 5;
  constexpr std::uint64_t kIdleStateWaitMs = 50;

  class SignlangAiAccumulator {
  public:
    using Clock = std::chrono::steady_clock;

    void append(std::string text) {
      if (text.empty()) {
        return;
      }
      if (buffer_.empty()) {
        last_appended_at_ = Clock::now();
      }
      last_appended_at_ = Clock::now();
      buffer_ += text;
    }

    [[nodiscard]] auto idle_for(std::uint64_t timeout_ms) const -> bool {
      if (buffer_.empty() || !last_appended_at_.has_value()) {
        return false;
      }
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - last_appended_at_.value());
      return elapsed.count() >= static_cast<std::int64_t>(timeout_ms);
    }

    [[nodiscard]] auto empty() const -> bool { return buffer_.empty(); }

    auto take() -> std::string {
      auto prompt = std::move(buffer_);
      buffer_.clear();
      last_appended_at_.reset();
      return prompt;
    }

    void clear() {
      buffer_.clear();
      last_appended_at_.reset();
    }

  private:
    std::string buffer_;
    std::optional<Clock::time_point> last_appended_at_;
  };

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

  void display_llm_response(signlang::dataflow_dispatcher::IpcDisplayClient& display_client,
                            const std::string& text) {
    const auto response = display_client.set_second_line(text);
    if (response.status != signlang::peripheral_service::DisplayStatus::Ok) {
      spdlog::warn("peripheral_service rejected LLM display text: {}", response.message.data());
    } else {
      spdlog::info("Forwarded LLM response to peripheral_service display");
    }
  }

  void flush_signlang_ai_prompt(SignlangAiAccumulator& accumulator,
                                signlang::dataflow_dispatcher::IpcLlmClient& llm_client,
                                signlang::dataflow_dispatcher::IpcDisplayClient& display_client) {
    if (accumulator.empty()) {
      return;
    }

    const auto prompt = accumulator.take();
    const auto llm_response = llm_client.send_prompt(prompt);
    if (llm_response.status != signlang::llm_client::LlmResponseStatus::Ok) {
      spdlog::warn("llm_client rejected signlang AI prompt '{}': {}", prompt, llm_response.error_message.data());
      return;
    }

    const auto response_text = signlang::common::fixed_string_to_string(llm_response.text);
    if (response_text.empty()) {
      spdlog::warn("llm_client returned an empty response for signlang AI prompt '{}'", prompt);
      return;
    }

    display_llm_response(display_client, response_text);
  }

} // namespace

auto main(int argc, char** argv) -> int {
  using signlang::dataflow_dispatcher::IpcDisplayClient;
  using signlang::dataflow_dispatcher::IpcLlmClient;
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
    spdlog::info("llm_client service: {}", options.llm_client_service_name);
    spdlog::info("peripheral display service: {}", options.peripheral_display_service_name);
    spdlog::info("SignLanguageAi idle flush timeout: {} ms", options.signlang_ai_window_ms);

    auto state_subscriber = IpcStateSubscriber{options.state_event_service_name, options.state_blackboard_service_name};
    auto tts_client = IpcSpeechTtsClient{options.speech_tts_service_name};
    auto llm_client = IpcLlmClient{options.llm_client_service_name};
    auto display_client = IpcDisplayClient{options.peripheral_display_service_name};
    auto signlang_subscriber = std::optional<IpcSignlangResultSubscriber>{};
    auto signlang_ai_accumulator = SignlangAiAccumulator{};
    auto active_upstream = RequiredUpstream::None;
    auto current_state = state_subscriber.current_state();

    apply_upstream_for_state(current_state, options, signlang_subscriber, active_upstream);

    while (!signlang::runtime::shutdown_requested()) {
      if (state_subscriber.poll_state_change()) {
        current_state = state_subscriber.current_state();
        signlang_ai_accumulator.clear();
        apply_upstream_for_state(current_state, options, signlang_subscriber, active_upstream);
      }

      if (!signlang_subscriber.has_value()) {
        if (state_subscriber.wait_for_state_change(kIdleStateWaitMs)) {
          current_state = state_subscriber.current_state();
          signlang_ai_accumulator.clear();
          apply_upstream_for_state(current_state, options, signlang_subscriber, active_upstream);
        }
        continue;
      }

      if (current_state == signlang::state_machine::AppState::SignLanguageAi &&
          signlang_ai_accumulator.idle_for(options.signlang_ai_window_ms)) {
        flush_signlang_ai_prompt(signlang_ai_accumulator, llm_client, display_client);
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

        switch (current_state) {
        case signlang::state_machine::AppState::SignLanguageChat: {
          const auto response = tts_client.send_text(text.value());
          if (response.status != signlang::speech_tts::SpeechTtsStatus::Ok) {
            spdlog::warn("speech_tts rejected signlang result '{}': {}", text.value(), response.message.data());
          } else {
            spdlog::info("Forwarded signlang result to speech_tts: {}", text.value());
          }
          break;
        }
        case signlang::state_machine::AppState::SignLanguageAi:
          signlang_ai_accumulator.append(text.value());
          spdlog::info("Appended signlang AI fragment: {}", text.value());
          break;
        case signlang::state_machine::AppState::Normal:
        case signlang::state_machine::AppState::Asr:
        case signlang::state_machine::AppState::DangerousSound:
          break;
        }
      });
    }

    signlang_subscriber.reset();
    return 0;
  });
}
