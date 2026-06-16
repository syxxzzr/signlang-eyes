#ifndef SIGNLANG_EYES_EDGEAI_SPEECH_ASR_SPEECH_ASR_RESULT_HPP
#define SIGNLANG_EYES_EDGEAI_SPEECH_ASR_SPEECH_ASR_RESULT_HPP

#include <array>
#include <cstdint>
#include <type_traits>

namespace signlang::speech_asr {

  constexpr std::uint32_t kWhisperSampleRateHz = 16000;
  constexpr std::uint32_t kDefaultWindowMs = 15000;
  constexpr double kDefaultOverlapRatio = 0.2;
  constexpr std::uint32_t kMaxTranscriptLength = 512;
  constexpr std::uint32_t kMaxLanguageCodeLength = 8;

  enum class AsrLanguage : std::uint32_t {
    English = 0,
    Chinese = 1,
  };

  // ASR enable/disable state stored in Blackboard
  // TODO: Replace placeholder value 0 with actual enabled state identifier
  enum class AsrState : std::uint32_t {
    Enabled = 0,   // ASR enabled (currently using 0 as placeholder)
    Disabled = 1,  // ASR disabled
  };

  struct AsrStateKey {
    static constexpr const char* IOX2_TYPE_NAME = "signlang_speech_asr_state_key";
    std::uint32_t id;

    auto operator==(const AsrStateKey& other) const -> bool { return id == other.id; }
    auto operator!=(const AsrStateKey& other) const -> bool { return id != other.id; }
  };

  static_assert(std::is_trivially_copyable_v<AsrStateKey>);
  static_assert(std::is_trivially_copyable_v<AsrState>);

  struct SpeechAsrResult {
    static constexpr const char* IOX2_TYPE_NAME = "signlang_speech_asr_result";

    std::uint64_t sequence_number;
    std::uint64_t timestamp_ns;
    std::uint64_t audio_start_sample_index;
    std::uint64_t audio_end_sample_index;
    std::uint64_t latest_audio_sequence_number;
    std::uint64_t latest_audio_timestamp_ns;
    std::uint32_t latest_audio_sample_rate_hz;
    std::uint32_t latest_audio_publish_period_ms;
    std::uint32_t latest_audio_frame_count;
    std::uint16_t latest_audio_channel_count;
    std::uint16_t latest_audio_bits_per_sample;
    std::uint32_t audio_sample_rate_hz;
    std::uint32_t window_ms;
    std::uint32_t hop_ms;
    std::uint32_t model_input_sample_count;
    std::uint32_t mel_frame_count;
    std::uint32_t decoded_token_count;
    AsrLanguage language;
    float overlap_ratio;
    float encoder_time_ms;
    float decoder_time_ms;
    float inference_time_ms;
    std::array<char, kMaxLanguageCodeLength> language_code;
    std::array<char, kMaxTranscriptLength> transcript;
  };

  static_assert(std::is_trivially_copyable_v<AsrStateKey>);
  static_assert(std::is_trivially_copyable_v<AsrState>);
  static_assert(std::is_trivially_copyable_v<SpeechAsrResult>);

  auto language_code(AsrLanguage language) -> const char*;

} // namespace signlang::speech_asr

#endif // SIGNLANG_EYES_EDGEAI_SPEECH_ASR_SPEECH_ASR_RESULT_HPP
