#ifndef SIGNLANG_EYES_AUDIO_FRONTEND_SOUND_SOURCE_LOCALIZATION_HPP
#define SIGNLANG_EYES_AUDIO_FRONTEND_SOUND_SOURCE_LOCALIZATION_HPP

#include "audio_format.hpp"

#include <array>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace signlang::audio_frontend {

  constexpr float kDefaultLocalizationTdoaWeight = 0.7F;
  constexpr float kDefaultLocalizationRmsWeight = 0.3F;

  struct SoundSourceLocalizationKey {
    static constexpr const char* IOX2_TYPE_NAME = "signlang_audio_sound_source_localization_key";
    std::uint32_t id;

    auto operator==(const SoundSourceLocalizationKey& other) const -> bool { return id == other.id; }
    auto operator!=(const SoundSourceLocalizationKey& other) const -> bool { return id != other.id; }
  };

  struct SoundSourceLocalizationResult {
    std::uint64_t sequence_number;
    std::uint64_t timestamp_ns;
    std::uint32_t sample_rate_hz;
    std::uint32_t frame_count;
    std::uint16_t channel_count;
    std::uint16_t strongest_channel;
    float confidence;
    bool valid;
    std::array<float, kMaxChannelCount> proximity;
    std::array<float, kMaxChannelCount> rms;
    std::array<float, kMaxChannelCount> tdoa_score;
  };

  static_assert(std::is_trivially_copyable_v<SoundSourceLocalizationKey>);
  static_assert(std::is_trivially_copyable_v<SoundSourceLocalizationResult>);

  constexpr auto default_sound_source_localization_key() -> SoundSourceLocalizationKey {
    return SoundSourceLocalizationKey{.id = 0};
  }

  class SoundSourceLocalizer {
  public:
    SoundSourceLocalizer(float tdoa_weight, float rms_weight);

    auto estimate(const std::vector<std::int16_t>& interleaved_samples, AudioFormat format,
                  std::uint64_t sequence_number, std::uint64_t timestamp_ns) const -> SoundSourceLocalizationResult;

  private:
    struct LagEstimate {
      int lag_samples;
      float correlation;
    };

    static auto estimate_lag(const std::vector<std::int16_t>& interleaved_samples, std::uint32_t first_frame,
                             std::uint32_t frame_count, std::uint16_t channel_count, std::uint16_t channel_a,
                             std::uint16_t channel_b, std::uint32_t max_lag_samples) -> LagEstimate;

    float tdoa_weight_;
    float rms_weight_;
  };

} // namespace signlang::audio_frontend

#endif // SIGNLANG_EYES_AUDIO_FRONTEND_SOUND_SOURCE_LOCALIZATION_HPP
