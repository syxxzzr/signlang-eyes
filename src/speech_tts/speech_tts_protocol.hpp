#ifndef SIGNLANG_EYES_SPEECH_TTS_PROTOCOL_HPP
#define SIGNLANG_EYES_SPEECH_TTS_PROTOCOL_HPP

#include <array>
#include <cstdint>
#include <type_traits>

namespace signlang::speech_tts {

  constexpr auto kSpeechTtsTextLength = std::uint32_t{256};
  constexpr auto kSpeechTtsMessageLength = std::uint32_t{128};

  enum class SpeechTtsStatus : std::uint32_t {
    Ok = 0,
    BadRequest = 1,
    Failed = 2,
  };

  struct SpeechTtsRequest {
    static constexpr const char* IOX2_TYPE_NAME = "signlang_speech_tts_request";

    std::uint32_t request_id;
    std::array<char, kSpeechTtsTextLength> text;
  };

  struct SpeechTtsResponse {
    static constexpr const char* IOX2_TYPE_NAME = "signlang_speech_tts_response";

    SpeechTtsStatus status;
    std::uint32_t request_id;
    std::uint64_t generation;
    std::array<char, kSpeechTtsMessageLength> message;
  };

  static_assert(std::is_trivially_copyable_v<SpeechTtsRequest>);
  static_assert(std::is_trivially_copyable_v<SpeechTtsResponse>);

} // namespace signlang::speech_tts

#endif // SIGNLANG_EYES_SPEECH_TTS_PROTOCOL_HPP
