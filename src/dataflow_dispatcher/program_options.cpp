#include "program_options.hpp"

#include "common/logging_cli.hpp"
#include "cxxopts.hpp"

#include <stdexcept>
#include <string>

namespace signlang::dataflow_dispatcher {
  namespace {
    constexpr std::uint64_t kDefaultSubscriberBufferSize = 2;
    constexpr std::uint64_t kDefaultSignlangAiWindowMs = 5000;
  }

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult {
    cxxopts::Options options{"signlang_eyes_dataflow_dispatcher",
                             "Dispatch upstream dataflow by global app state."};

    options.add_options()("state-event-service", "iceoryx2 app state event service name",
                          cxxopts::value<std::string>())(
        "state-blackboard-service", "iceoryx2 app state blackboard service name", cxxopts::value<std::string>())(
        "signlang-result-service", "signlang_det result publish-subscribe service name",
        cxxopts::value<std::string>())(
        "speech-asr-result-service", "speech_asr result publish-subscribe service name",
        cxxopts::value<std::string>())("speech-tts-service", "speech_tts request-response service name",
                                       cxxopts::value<std::string>())(
        "llm-client-service", "llm_client request-response service name", cxxopts::value<std::string>())(
        "peripheral-display-service", "peripheral_service display request-response service name",
        cxxopts::value<std::string>())(
        "subscriber-buffer", "iceoryx2 subscriber queue size",
        cxxopts::value<std::uint64_t>()->default_value(std::to_string(kDefaultSubscriberBufferSize)))(
        "signlang-ai-window-ms", "SignLanguageAi idle timeout after the latest signlang_det result in milliseconds",
        cxxopts::value<std::uint64_t>()->default_value(std::to_string(kDefaultSignlangAiWindowMs)))(
        "h,help", "Print usage");
    signlang::logging::add_cli_options(options);

    const auto parsed_options = options.parse(argc, argv);
    if (parsed_options.count("help") != 0) {
      return ProgramUsage{options.help()};
    }

    if (parsed_options.count("state-event-service") == 0 ||
        parsed_options.count("state-blackboard-service") == 0 ||
        parsed_options.count("signlang-result-service") == 0 ||
        parsed_options.count("speech-asr-result-service") == 0 ||
        parsed_options.count("speech-tts-service") == 0 ||
        parsed_options.count("llm-client-service") == 0 ||
        parsed_options.count("peripheral-display-service") == 0) {
      throw std::runtime_error("--state-event-service, --state-blackboard-service, --signlang-result-service, "
                               "--speech-asr-result-service, --speech-tts-service, --llm-client-service, and "
                               "--peripheral-display-service are required.\n\n" +
                               options.help());
    }

    const auto subscriber_buffer_size = parsed_options["subscriber-buffer"].as<std::uint64_t>();
    if (subscriber_buffer_size == 0) {
      throw std::runtime_error("--subscriber-buffer must be greater than 0");
    }
    const auto signlang_ai_window_ms = parsed_options["signlang-ai-window-ms"].as<std::uint64_t>();
    if (signlang_ai_window_ms == 0) {
      throw std::runtime_error("--signlang-ai-window-ms must be greater than 0");
    }

    return ProgramOptionsParseResult{ProgramOptions{
        parsed_options["state-event-service"].as<std::string>(),
        parsed_options["state-blackboard-service"].as<std::string>(),
        parsed_options["signlang-result-service"].as<std::string>(),
        parsed_options["speech-asr-result-service"].as<std::string>(),
        parsed_options["speech-tts-service"].as<std::string>(),
        parsed_options["llm-client-service"].as<std::string>(),
        parsed_options["peripheral-display-service"].as<std::string>(),
        subscriber_buffer_size,
        signlang_ai_window_ms,
        signlang::logging::parse_cli_options(parsed_options),
    }};
  }

} // namespace signlang::dataflow_dispatcher
