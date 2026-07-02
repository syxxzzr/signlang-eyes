#ifndef SIGNLANG_EYES_LLM_CLIENT_LLM_PROTOCOL_HPP
#define SIGNLANG_EYES_LLM_CLIENT_LLM_PROTOCOL_HPP

#include <array>
#include <cstdint>
#include <type_traits>

namespace signlang::llm_client {

  constexpr auto kMaxPromptLength = std::uint32_t{8192};
  constexpr auto kMaxResponseTextLength = std::uint32_t{8192};
  constexpr auto kMaxErrorMessageLength = std::uint32_t{512};

  enum class LlmResponseStatus : std::uint32_t {
    Ok = 0,
    BadRequest = 1,
    ApiError = 2,
    NetworkError = 3,
    ResponseTooLarge = 4,
  };

  struct LlmRequest {
    static constexpr const char* IOX2_TYPE_NAME = "signlang_llm_request";

    std::uint32_t request_id;
    std::array<char, kMaxPromptLength> prompt;
  };

  struct LlmResponse {
    static constexpr const char* IOX2_TYPE_NAME = "signlang_llm_response";

    LlmResponseStatus status;
    std::uint32_t request_id;
    std::uint32_t http_status;
    std::array<char, kMaxResponseTextLength> text;
    std::array<char, kMaxErrorMessageLength> error_message;
  };

  static_assert(std::is_trivially_copyable_v<LlmRequest>);
  static_assert(std::is_trivially_copyable_v<LlmResponse>);

} // namespace signlang::llm_client

#endif // SIGNLANG_EYES_LLM_CLIENT_LLM_PROTOCOL_HPP
