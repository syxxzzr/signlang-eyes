#include "program_options.hpp"

#include "common/logging_cli.hpp"
#include "cxxopts.hpp"

#include <stdexcept>
#include <string>

namespace signlang::dataflow_dispatcher {
  namespace {
    constexpr std::uint64_t kDefaultSubscriberBufferSize = 2;
  }

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult {
    cxxopts::Options options{"signlang_eyes_dataflow_dispatcher",
                             "Dispatch upstream dataflow by global app state."};

    options.add_options()("state-event-service", "iceoryx2 app state event service name",
                          cxxopts::value<std::string>())(
        "state-blackboard-service", "iceoryx2 app state blackboard service name", cxxopts::value<std::string>())(
        "signlang-result-service", "signlang_det result publish-subscribe service name",
        cxxopts::value<std::string>())("speech-tts-service", "speech_tts request-response service name",
                                       cxxopts::value<std::string>())(
        "subscriber-buffer", "iceoryx2 subscriber queue size",
        cxxopts::value<std::uint64_t>()->default_value(std::to_string(kDefaultSubscriberBufferSize)))(
        "h,help", "Print usage");
    signlang::logging::add_cli_options(options);

    const auto parsed_options = options.parse(argc, argv);
    if (parsed_options.count("help") != 0) {
      return ProgramUsage{options.help()};
    }

    if (parsed_options.count("state-event-service") == 0 ||
        parsed_options.count("state-blackboard-service") == 0 ||
        parsed_options.count("signlang-result-service") == 0 || parsed_options.count("speech-tts-service") == 0) {
      throw std::runtime_error("--state-event-service, --state-blackboard-service, --signlang-result-service, "
                               "and --speech-tts-service are required.\n\n" +
                               options.help());
    }

    const auto subscriber_buffer_size = parsed_options["subscriber-buffer"].as<std::uint64_t>();
    if (subscriber_buffer_size == 0) {
      throw std::runtime_error("--subscriber-buffer must be greater than 0");
    }

    return ProgramOptionsParseResult{ProgramOptions{
        parsed_options["state-event-service"].as<std::string>(),
        parsed_options["state-blackboard-service"].as<std::string>(),
        parsed_options["signlang-result-service"].as<std::string>(),
        parsed_options["speech-tts-service"].as<std::string>(),
        subscriber_buffer_size,
        signlang::logging::parse_cli_options(parsed_options),
    }};
  }

} // namespace signlang::dataflow_dispatcher
