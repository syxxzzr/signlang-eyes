#include "common/fixed_string.hpp"
#include "common/runtime.hpp"
#include "iceoryx_gateway.hpp"
#include "llm_protocol.hpp"
#include "openai_client.hpp"
#include "program_options.hpp"
#include "spdlog/spdlog.h"

#include <boost/asio.hpp>

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace signlang::llm_client {
  namespace {

    namespace asio = boost::asio;

    auto read_text_file(const std::string& path) -> std::string {
      std::ifstream input{path};
      if (!input) {
        throw std::runtime_error("Failed to open LLM system prompt file: " + path);
      }

      return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    }

    auto make_response(const LlmRequest& request, LlmResponseStatus status, std::uint32_t http_status,
                       const std::string& text, const std::string& error_message) -> LlmResponse {
      auto response = LlmResponse{};
      response.status = status;
      response.request_id = request.request_id;
      response.http_status = http_status;
      common::copy_fixed_string(text, response.text);
      common::copy_fixed_string(error_message, response.error_message);
      return response;
    }

    auto response_from_completion(const LlmRequest& request, const LlmCompletionResult& completion) -> LlmResponse {
      if (!completion.error_message.empty()) {
        return make_response(request, LlmResponseStatus::ApiError, completion.http_status, {}, completion.error_message);
      }

      if (completion.text.size() >= kMaxResponseTextLength) {
        return make_response(request, LlmResponseStatus::ResponseTooLarge, completion.http_status, {},
                             "LLM response text exceeds IPC response capacity");
      }

      return make_response(request, LlmResponseStatus::Ok, completion.http_status, completion.text, {});
    }

    auto run_completion(asio::io_context& io_context, OpenAiClient& client, const std::string& prompt)
        -> LlmCompletionResult {
      auto future = asio::co_spawn(io_context, client.complete(prompt), asio::use_future);

      while (future.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready) {
        io_context.restart();
        io_context.run_for(std::chrono::milliseconds{50});
        if (runtime::shutdown_requested()) {
          throw std::runtime_error("Shutdown requested while waiting for LLM API response");
        }
      }

      return future.get();
    }

  } // namespace
} // namespace signlang::llm_client

auto main(int argc, char** argv) -> int {
  using signlang::llm_client::IpcLlmServer;
  using signlang::llm_client::LlmResponseStatus;
  using signlang::llm_client::OpenAiClient;
  using signlang::llm_client::parse_program_options;

  return signlang::runtime::run_module(argc, argv, parse_program_options, [](const auto& options) {
    spdlog::info("Starting LLM client");
    spdlog::info("Service: {}", options.service_name);
    spdlog::info("Base URL: {}", options.base_url);
    spdlog::info("Model: {}", options.model);
    spdlog::info("System prompt file: {}", options.system_prompt_file);

    auto builtin_prompt = signlang::llm_client::read_text_file(options.system_prompt_file);
    spdlog::info("Loaded builtin prompt ({} bytes)", builtin_prompt.size());

    boost::asio::io_context io_context;
    OpenAiClient client{io_context, options, std::move(builtin_prompt)};
    IpcLlmServer server{options.service_name};

    while (!signlang::runtime::shutdown_requested()) {
      if (!server.wait_for_work(50)) {
        continue;
      }

      server.process_pending_requests([&](const signlang::llm_client::LlmRequest& request) {
        const auto prompt = signlang::common::fixed_string_to_string(request.prompt);
        if (prompt.empty()) {
          return signlang::llm_client::make_response(request, LlmResponseStatus::BadRequest, 0, {},
                                                     "LLM prompt must not be empty");
        }

        try {
          const auto completion = signlang::llm_client::run_completion(io_context, client, prompt);
          return signlang::llm_client::response_from_completion(request, completion);
        } catch (const std::exception& error) {
          spdlog::warn("LLM request {} failed: {}", request.request_id, error.what());
          return signlang::llm_client::make_response(request, LlmResponseStatus::NetworkError, 0, {}, error.what());
        }
      });
    }

    return 0;
  });
}
