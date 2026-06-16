#ifndef SIGNLANG_EYES_EDGEAI_AUDIO_FRONTEND_AUDIO_FORMAT_HPP
#define SIGNLANG_EYES_EDGEAI_AUDIO_FRONTEND_AUDIO_FORMAT_HPP

#include <cstdint>
#include <optional>

namespace signlang::audio_frontend {

  constexpr std::uint32_t kDefaultSampleRateHz = 16000;
  constexpr std::uint16_t kDefaultChannelCount = 1;
  constexpr std::uint16_t kBitsPerSample = 16;
  constexpr std::uint32_t kDefaultPublishPeriodMs = 100;
  constexpr std::uint32_t kMaxPublishPeriodMs = 1000;
  constexpr std::uint32_t kMinSampleRateHz = 8000;
  constexpr std::uint32_t kMaxSampleRateHz = 192000;
  constexpr std::uint16_t kMinChannelCount = 1;
  constexpr std::uint16_t kMaxChannelCount = 8;

  struct AudioFormat {
    std::uint32_t sample_rate_hz;
    std::uint16_t channel_count;
  };

  struct AudioFormatRequest {
    std::optional<std::uint32_t> sample_rate_hz;
    std::optional<std::uint16_t> channel_count;
  };

  constexpr auto is_valid_sample_rate(std::uint32_t sample_rate_hz) -> bool {
    return sample_rate_hz >= kMinSampleRateHz && sample_rate_hz <= kMaxSampleRateHz;
  }

  constexpr auto is_valid_channel_count(std::uint16_t channel_count) -> bool {
    return channel_count >= kMinChannelCount && channel_count <= kMaxChannelCount;
  }

} // namespace signlang::audio_frontend

#endif // SIGNLANG_EYES_EDGEAI_AUDIO_FRONTEND_AUDIO_FORMAT_HPP
