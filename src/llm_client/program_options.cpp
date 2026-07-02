#include "program_options.hpp"

#include "common/logging_cli.hpp"
#include "cxxopts.hpp"

#include <stdexcept>
#include <string>

namespace signlang::llm_client {
  namespace {

    constexpr auto kDefaultBaseUrl = "https://api.openai.com/v1";
    constexpr auto kDefaultModel = "gpt-5.5";
    constexpr auto kDefaultSystemPromptFile = "conf/system_prompt.txt";
    constexpr std::uint32_t kDefaultRequestTimeoutMs = 60000;
    constexpr std::uint32_t kDefaultConcurrency = 2;
    constexpr std::uint32_t kDefaultWorkerQueueCapacity = 8;

  } // namespace

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult {
    cxxopts::Options options{
        "signlang_eyes_llm_client",
        "Handle iceoryx2 LLM requests through an OpenAI-compatible non-streaming chat completion API."};

    options.add_options()("s,service", "iceoryx2 LLM request-response service name",
                          cxxopts::value<std::string>())(
        "base-url", "OpenAI-compatible API base URL, for example https://api.openai.com/v1",
        cxxopts::value<std::string>()->default_value(kDefaultBaseUrl))(
        "api-key", "API secret key. If empty, Authorization is omitted",
        cxxopts::value<std::string>()->default_value(""))(
        "model", "OpenAI-compatible model name", cxxopts::value<std::string>()->default_value(kDefaultModel))(
        "system-prompt-file", "Path to builtin system prompt text prepended to every user prompt",
        cxxopts::value<std::string>()->default_value(kDefaultSystemPromptFile))(
        "request-timeout-ms", "HTTP request timeout in milliseconds",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultRequestTimeoutMs)))(
        "concurrency", "Number of concurrent LLM HTTP worker threads",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultConcurrency)))(
        "worker-queue-capacity", "Maximum queued LLM requests per worker",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultWorkerQueueCapacity)))(
        "h,help", "Print usage");
    signlang::logging::add_cli_options(options);

    const auto parsed_options = options.parse(argc, argv);
    if (parsed_options.count("help") != 0) {
      return ProgramUsage{options.help()};
    }

    if (parsed_options.count("service") == 0) {
      throw std::runtime_error("Option --service is required.\n\n" + options.help());
    }

    auto base_url = parsed_options["base-url"].as<std::string>();
    if (base_url.empty()) {
      throw std::runtime_error("--base-url must not be empty");
    }

    auto model = parsed_options["model"].as<std::string>();
    if (model.empty()) {
      throw std::runtime_error("--model must not be empty");
    }

    const auto timeout_ms = parsed_options["request-timeout-ms"].as<std::uint32_t>();
    if (timeout_ms == 0) {
      throw std::runtime_error("--request-timeout-ms must be greater than 0");
    }

    const auto concurrency = parsed_options["concurrency"].as<std::uint32_t>();
    if (concurrency == 0) {
      throw std::runtime_error("--concurrency must be greater than 0");
    }

    const auto worker_queue_capacity = parsed_options["worker-queue-capacity"].as<std::uint32_t>();
    if (worker_queue_capacity == 0) {
      throw std::runtime_error("--worker-queue-capacity must be greater than 0");
    }

    return ProgramOptionsParseResult{ProgramOptions{parsed_options["service"].as<std::string>(),
                                                    std::move(base_url),
                                                    parsed_options["api-key"].as<std::string>(),
                                                    std::move(model),
                                                    parsed_options["system-prompt-file"].as<std::string>(),
                                                    timeout_ms,
                                                    concurrency,
                                                    worker_queue_capacity,
                                                    signlang::logging::parse_cli_options(parsed_options)}};
  }

} // namespace signlang::llm_client
