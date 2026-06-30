#ifndef SIGNLANG_EYES_LLM_CLIENT_OPENAI_CLIENT_HPP
#define SIGNLANG_EYES_LLM_CLIENT_OPENAI_CLIENT_HPP

#include "program_options.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>

#include <cstdint>
#include <string>

namespace signlang::llm_client {

  struct LlmCompletionResult {
    std::uint32_t http_status;
    std::string text;
    std::string error_message;
  };

  class OpenAiClient {
  public:
    OpenAiClient(boost::asio::io_context& io_context, ProgramOptions options, std::string builtin_prompt);

    auto complete(const std::string& prompt) -> boost::asio::awaitable<LlmCompletionResult>;

  private:
    struct ParsedUrl {
      std::string scheme;
      std::string host;
      std::string port;
      std::string target;
    };

    [[nodiscard]] static auto parse_base_url(const std::string& base_url) -> ParsedUrl;
    [[nodiscard]] auto build_request_body(const std::string& prompt) const -> std::string;
    [[nodiscard]] static auto parse_response(std::uint32_t http_status, const std::string& body)
        -> LlmCompletionResult;
    [[nodiscard]] static auto completion_target(std::string path) -> std::string;

    boost::asio::io_context& io_context_;
    ProgramOptions options_;
    std::string builtin_prompt_;
    ParsedUrl url_;
  };

} // namespace signlang::llm_client

#endif // SIGNLANG_EYES_LLM_CLIENT_OPENAI_CLIENT_HPP
