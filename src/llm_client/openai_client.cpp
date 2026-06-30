#include "openai_client.hpp"

#include <utility>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/json.hpp>
#include <boost/json/src.hpp>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>
#include <string_view>

#include <openssl/ssl.h>

namespace signlang::llm_client {
  namespace {

    namespace asio = boost::asio;
    namespace beast = boost::beast;
    namespace http = beast::http;
    namespace json = boost::json;
    using Tcp = asio::ip::tcp;

    auto remove_trailing_slashes(std::string value) -> std::string {
      while (value.size() > 1U && value.back() == '/') {
        value.pop_back();
      }
      return value;
    }

    auto string_value(const json::value& value) -> std::string {
      if (value.is_string()) {
        return std::string{value.as_string().c_str()};
      }
      return {};
    }

    auto error_from_json(const json::value& parsed) -> std::string {
      if (!parsed.is_object()) {
        return {};
      }

      const auto& root = parsed.as_object();
      if (const auto* error = root.if_contains("error")) {
        if (error->is_object()) {
          if (const auto* message = error->as_object().if_contains("message")) {
            return string_value(*message);
          }
        }
        return json::serialize(*error);
      }
      return {};
    }

  } // namespace

  OpenAiClient::OpenAiClient(boost::asio::io_context& io_context, ProgramOptions options, std::string builtin_prompt) :
      io_context_{io_context},
      options_{std::move(options)}, builtin_prompt_{std::move(builtin_prompt)}, url_{parse_base_url(options_.base_url)} {
  }

  auto OpenAiClient::complete(const std::string& prompt) -> boost::asio::awaitable<LlmCompletionResult> {
    const auto executor = co_await asio::this_coro::executor;
    Tcp::resolver resolver{executor};
    const auto results = co_await resolver.async_resolve(url_.host, url_.port, asio::use_awaitable);

    http::request<http::string_body> request{http::verb::post, url_.target, 11};
    request.set(http::field::host, url_.host);
    request.set(http::field::user_agent, "signlang-eyes-llm-client");
    request.set(http::field::content_type, "application/json");
    request.set(http::field::accept, "application/json");
    if (!options_.api_key.empty()) {
      request.set(http::field::authorization, "Bearer " + options_.api_key);
    }
    request.body() = build_request_body(prompt);
    request.prepare_payload();

    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    const auto timeout = std::chrono::milliseconds{options_.request_timeout_ms};

    if (url_.scheme == "https") {
      asio::ssl::context ssl_context{asio::ssl::context::tls_client};
      ssl_context.set_default_verify_paths();
      ssl_context.set_verify_mode(asio::ssl::verify_peer);

      beast::ssl_stream<beast::tcp_stream> stream{executor, ssl_context};
      if (SSL_set_tlsext_host_name(stream.native_handle(), url_.host.c_str()) != 1) {
        throw std::runtime_error("Failed to set TLS SNI host name");
      }

      beast::get_lowest_layer(stream).expires_after(timeout);
      co_await beast::get_lowest_layer(stream).async_connect(results, asio::use_awaitable);
      co_await stream.async_handshake(asio::ssl::stream_base::client, asio::use_awaitable);
      co_await http::async_write(stream, request, asio::use_awaitable);
      co_await http::async_read(stream, buffer, response, asio::use_awaitable);

      beast::error_code ignored;
      beast::get_lowest_layer(stream).expires_after(timeout);
      co_await stream.async_shutdown(asio::redirect_error(asio::use_awaitable, ignored));
    } else if (url_.scheme == "http") {
      beast::tcp_stream stream{executor};
      stream.expires_after(timeout);
      co_await stream.async_connect(results, asio::use_awaitable);
      co_await http::async_write(stream, request, asio::use_awaitable);
      co_await http::async_read(stream, buffer, response, asio::use_awaitable);

      beast::error_code ignored;
      stream.socket().shutdown(Tcp::socket::shutdown_both, ignored);
    } else {
      throw std::runtime_error("Unsupported URL scheme: " + url_.scheme);
    }

    co_return parse_response(static_cast<std::uint32_t>(response.result_int()), response.body());
  }

  auto OpenAiClient::parse_base_url(const std::string& base_url) -> ParsedUrl {
    const auto scheme_pos = base_url.find("://");
    if (scheme_pos == std::string::npos) {
      throw std::runtime_error("--base-url must start with http:// or https://");
    }

    auto scheme = base_url.substr(0, scheme_pos);
    auto rest = base_url.substr(scheme_pos + 3U);
    const auto path_pos = rest.find('/');
    auto authority = path_pos == std::string::npos ? rest : rest.substr(0, path_pos);
    auto path = path_pos == std::string::npos ? std::string{"/"} : rest.substr(path_pos);

    if (authority.empty()) {
      throw std::runtime_error("--base-url host must not be empty");
    }

    auto host = authority;
    auto port = scheme == "https" ? std::string{"443"} : std::string{"80"};
    if (const auto port_pos = authority.rfind(':'); port_pos != std::string::npos) {
      host = authority.substr(0, port_pos);
      port = authority.substr(port_pos + 1U);
      if (host.empty() || port.empty()) {
        throw std::runtime_error("--base-url authority must contain a valid host and port");
      }
    }

    return ParsedUrl{
        .scheme = std::move(scheme),
        .host = std::move(host),
        .port = std::move(port),
        .target = completion_target(std::move(path)),
    };
  }

  auto OpenAiClient::completion_target(std::string path) -> std::string {
    if (path.empty()) {
      path = "/";
    }
    if (const auto query_pos = path.find('?'); query_pos != std::string::npos) {
      path.erase(query_pos);
    }

    path = remove_trailing_slashes(std::move(path));
    if (path == "/chat/completions" || path.ends_with("/chat/completions")) {
      return path;
    }
    if (path == "/") {
      return "/chat/completions";
    }
    return path + "/chat/completions";
  }

  auto OpenAiClient::build_request_body(const std::string& prompt) const -> std::string {
    auto merged_prompt = builtin_prompt_;
    if (!merged_prompt.empty() && !prompt.empty()) {
      merged_prompt += "\n\n";
    }
    merged_prompt += prompt;

    json::object message;
    message["role"] = "user";
    message["content"] = std::move(merged_prompt);

    json::array messages;
    messages.emplace_back(std::move(message));

    json::object root;
    root["model"] = options_.model;
    root["stream"] = false;
    root["messages"] = std::move(messages);
    return json::serialize(root);
  }

  auto OpenAiClient::parse_response(std::uint32_t http_status, const std::string& body) -> LlmCompletionResult {
    boost::system::error_code parse_error;
    const auto parsed = json::parse(body, parse_error);
    if (parse_error) {
      return LlmCompletionResult{
          .http_status = http_status,
          .text = {},
          .error_message = "Failed to parse LLM API JSON response: " + parse_error.message(),
      };
    }

    if (http_status < 200U || http_status >= 300U) {
      auto message = error_from_json(parsed);
      if (message.empty()) {
        message = "LLM API returned HTTP " + std::to_string(http_status);
      }
      return LlmCompletionResult{.http_status = http_status, .text = {}, .error_message = std::move(message)};
    }

    if (!parsed.is_object()) {
      return LlmCompletionResult{.http_status = http_status, .text = {}, .error_message = "LLM API response is not an object"};
    }

    const auto& root = parsed.as_object();
    const auto* choices = root.if_contains("choices");
    if (choices == nullptr || !choices->is_array() || choices->as_array().empty()) {
      return LlmCompletionResult{.http_status = http_status, .text = {}, .error_message = "LLM API response has no choices"};
    }

    const auto& first_choice = choices->as_array().front();
    if (!first_choice.is_object()) {
      return LlmCompletionResult{.http_status = http_status, .text = {}, .error_message = "LLM API choice is not an object"};
    }

    const auto& choice_object = first_choice.as_object();
    if (const auto* message = choice_object.if_contains("message"); message != nullptr && message->is_object()) {
      if (const auto* content = message->as_object().if_contains("content")) {
        const auto text = string_value(*content);
        if (!text.empty()) {
          return LlmCompletionResult{.http_status = http_status, .text = text, .error_message = {}};
        }
      }
    }

    if (const auto* text = choice_object.if_contains("text")) {
      const auto text_value = string_value(*text);
      if (!text_value.empty()) {
        return LlmCompletionResult{.http_status = http_status, .text = text_value, .error_message = {}};
      }
    }

    return LlmCompletionResult{.http_status = http_status, .text = {}, .error_message = "LLM API choice has no text content"};
  }

} // namespace signlang::llm_client
