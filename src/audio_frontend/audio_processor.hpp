#ifndef SIGNLANG_EYES_AUDIO_FRONTEND_AUDIO_PROCESSOR_HPP
#define SIGNLANG_EYES_AUDIO_FRONTEND_AUDIO_PROCESSOR_HPP

#include "audio_frame.hpp"

#include <cstdint>
#include <vector>

namespace signlang::audio_frontend {

  class AudioProcessor {
  public:
    AudioProcessor(AudioFormat input_format, AudioFormat output_format, std::uint32_t publish_period_ms);

    auto output_format() const -> AudioFormat;
    auto output_frame_count() const -> std::uint32_t;
    auto publish_period_ms() const -> std::uint32_t;
    void process(const std::vector<std::int16_t>& input_samples, AudioFrame& output_frame);

  private:
    auto input_frame_count() const -> std::uint32_t;
    auto mix_channel(const std::vector<std::int16_t>& input_samples, std::uint32_t input_frame_index,
                     std::uint16_t output_channel_index) const -> std::int32_t;

    AudioFormat input_format_;
    AudioFormat output_format_;
    std::uint32_t publish_period_ms_;
  };

} // namespace signlang::audio_frontend

#endif // SIGNLANG_EYES_AUDIO_FRONTEND_AUDIO_PROCESSOR_HPP
