#ifndef SIGNLANG_EYES_EDGEAI_AUDIO_FRONTEND_AUDIO_PROCESSOR_HPP
#define SIGNLANG_EYES_EDGEAI_AUDIO_FRONTEND_AUDIO_PROCESSOR_HPP

#include "audio_frame.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace signlang::audio_frontend {

  class AudioProcessor {
  public:
    AudioProcessor(AudioFormat input_format, AudioFormat output_format, std::uint32_t publish_period_ms,
                   bool enable_denoise);
    ~AudioProcessor();

    auto output_format() const -> AudioFormat;
    auto output_frame_count() const -> std::uint32_t;
    auto publish_period_ms() const -> std::uint32_t;
    void process(const std::vector<std::int16_t>& input_samples, AudioFrame& output_frame);

  private:
    auto input_frame_count() const -> std::uint32_t;
    auto mix_channel(const std::vector<std::int16_t>& input_samples, std::uint32_t input_frame_index,
                     std::uint16_t output_channel_index) const -> std::int32_t;
    void apply_wiener_filter(AudioFrame& output_frame);

    struct DenoiseFftWorkspace;

    AudioFormat input_format_;
    AudioFormat output_format_;
    std::uint32_t publish_period_ms_;
    bool enable_denoise_;
    std::vector<std::int16_t> denoise_scratch_;
    std::unique_ptr<DenoiseFftWorkspace> denoise_fft_workspace_;
  };

} // namespace signlang::audio_frontend

#endif // SIGNLANG_EYES_EDGEAI_AUDIO_FRONTEND_AUDIO_PROCESSOR_HPP
