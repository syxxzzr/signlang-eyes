#ifndef SIGNLANG_EYES_EDGEAI_ENV_SOUND_DET_ENV_SOUND_RESULT_HPP
#define SIGNLANG_EYES_EDGEAI_ENV_SOUND_DET_ENV_SOUND_RESULT_HPP

#include <array>
#include <cstdint>
#include <type_traits>

namespace signlang::env_sound_det {

  constexpr std::uint32_t kYamnetSampleRateHz = 16000;
  constexpr std::uint32_t kDefaultWindowMs = 3000;
  constexpr double kDefaultOverlapRatio = 0.2;
  constexpr std::uint32_t kMaxTopClassCount = 5;
  constexpr std::uint32_t kMaxClassLabelLength = 128;

  struct EnvSoundClassScore {
    std::uint32_t class_index;
    float score;
    std::array<char, kMaxClassLabelLength> label;
  };

  struct EnvSoundDetectionResult {
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
    std::uint32_t score_frame_count;
    std::uint32_t top_class_count;
    float overlap_ratio;
    float inference_time_ms;
    std::array<EnvSoundClassScore, kMaxTopClassCount> top_classes;
  };

  static_assert(std::is_trivially_copyable_v<EnvSoundDetectionResult>);

} // namespace signlang::env_sound_det

#endif // SIGNLANG_EYES_EDGEAI_ENV_SOUND_DET_ENV_SOUND_RESULT_HPP
