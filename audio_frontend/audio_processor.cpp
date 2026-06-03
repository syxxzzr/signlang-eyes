#include "audio_processor.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace signlang::audio_frontend {
  namespace {

    constexpr std::uint32_t kWienerWindowRadius = 2;
    constexpr std::int64_t kWienerGainScale = 32768;

    struct LocalStats {
      std::int32_t mean;
      std::int64_t variance;
    };

    auto frames_for_period(std::uint32_t sample_rate_hz, std::uint32_t publish_period_ms) -> std::uint32_t {
      return static_cast<std::uint32_t>((static_cast<std::uint64_t>(sample_rate_hz) * publish_period_ms) / 1000);
    }

    auto clamp_to_sample(std::int32_t value) -> std::int16_t {
      const auto clamped_value = std::clamp(value, static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min()),
                                            static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::max()));
      return static_cast<std::int16_t>(clamped_value);
    }

    auto local_stats(const std::vector<std::int16_t>& samples, std::uint32_t center_index) -> LocalStats {
      const auto frame_count = static_cast<std::uint32_t>(samples.size());
      const auto first_index = center_index > kWienerWindowRadius ? center_index - kWienerWindowRadius : 0;
      const auto last_index = std::min(frame_count, center_index + kWienerWindowRadius + 1);

      std::int64_t sum = 0;
      std::int64_t square_sum = 0;
      for (auto sample_index = first_index; sample_index < last_index; ++sample_index) {
        const auto sample = static_cast<std::int64_t>(samples[sample_index]);
        sum += sample;
        square_sum += sample * sample;
      }

      const auto count = static_cast<std::int64_t>(last_index - first_index);
      const auto mean = sum / count;
      const auto variance = std::max<std::int64_t>(0, (square_sum / count) - (mean * mean));
      return LocalStats{.mean = static_cast<std::int32_t>(mean), .variance = variance};
    }

    auto estimate_noise_variance(const std::vector<std::int16_t>& samples) -> std::int64_t {
      if (samples.empty()) {
        return 0;
      }

      std::int64_t min_variance = std::numeric_limits<std::int64_t>::max();
      constexpr auto kWindowStep = (kWienerWindowRadius * 2) + 1;
      const auto frame_count = static_cast<std::uint32_t>(samples.size());
      for (std::uint32_t sample_index = 0; sample_index < frame_count; sample_index += kWindowStep) {
        min_variance = std::min(min_variance, local_stats(samples, sample_index).variance);
      }

      return min_variance == std::numeric_limits<std::int64_t>::max() ? 0 : min_variance;
    }

  } // namespace

  AudioProcessor::AudioProcessor(AudioFormat input_format, AudioFormat output_format, std::uint32_t publish_period_ms,
                                 bool enable_denoise) :
      input_format_{input_format}, output_format_{output_format}, publish_period_ms_{publish_period_ms},
      enable_denoise_{enable_denoise} {
    if (output_format_.channel_count > input_format_.channel_count) {
      throw std::runtime_error("Output channel count must be less than or equal to input channel count");
    }

    if (output_format_.sample_rate_hz > input_format_.sample_rate_hz) {
      throw std::runtime_error("Output sample rate must be less than or equal to input sample rate");
    }

    const auto output_sample_count = output_frame_count() * output_format_.channel_count;
    if (output_sample_count > kMaxSamplesPerPacket) {
      throw std::runtime_error("Output audio frame exceeds iceoryx2 payload capacity");
    }

    if (enable_denoise_) {
      denoise_scratch_.resize(output_frame_count());
    }
  }

  auto AudioProcessor::output_format() const -> AudioFormat { return output_format_; }

  auto AudioProcessor::output_frame_count() const -> std::uint32_t {
    return frames_for_period(output_format_.sample_rate_hz, publish_period_ms_);
  }

  auto AudioProcessor::publish_period_ms() const -> std::uint32_t { return publish_period_ms_; }

  void AudioProcessor::process(const std::vector<std::int16_t>& input_samples, AudioFrame& output_frame) {
    const auto input_frames = input_frame_count();
    const auto output_frames = output_frame_count();
    const auto expected_input_samples = input_frames * input_format_.channel_count;
    if (input_samples.size() < expected_input_samples) {
      throw std::runtime_error("Captured audio sample count is smaller than expected");
    }

    if (input_format_.sample_rate_hz == output_format_.sample_rate_hz &&
        input_format_.channel_count == output_format_.channel_count) {
      const auto output_sample_count = static_cast<std::size_t>(output_frames) * output_format_.channel_count;
      std::copy_n(input_samples.begin(), output_sample_count, output_frame.samples.begin());
    } else if (input_format_.sample_rate_hz == output_format_.sample_rate_hz) {
      for (std::uint32_t output_frame_index = 0; output_frame_index < output_frames; ++output_frame_index) {
        for (std::uint16_t output_channel_index = 0; output_channel_index < output_format_.channel_count;
             ++output_channel_index) {
          const auto output_sample_index = (output_frame_index * output_format_.channel_count) + output_channel_index;
          output_frame.samples[output_sample_index] =
              clamp_to_sample(mix_channel(input_samples, output_frame_index, output_channel_index));
        }
      }
    } else {
      for (std::uint32_t output_frame_index = 0; output_frame_index < output_frames; ++output_frame_index) {
        const auto source_position = static_cast<std::uint64_t>(output_frame_index) * input_format_.sample_rate_hz;
        const auto input_frame_index = static_cast<std::uint32_t>(source_position / output_format_.sample_rate_hz);
        const auto interpolation_remainder = source_position % output_format_.sample_rate_hz;
        const auto next_input_frame_index = std::min(input_frame_index + 1, input_frames - 1);

        for (std::uint16_t output_channel_index = 0; output_channel_index < output_format_.channel_count;
             ++output_channel_index) {
          const auto output_sample_index = (output_frame_index * output_format_.channel_count) + output_channel_index;
          const auto current_sample =
              static_cast<std::int64_t>(mix_channel(input_samples, input_frame_index, output_channel_index));
          const auto next_sample =
              static_cast<std::int64_t>(mix_channel(input_samples, next_input_frame_index, output_channel_index));
          const auto interpolated_sample =
              ((current_sample * static_cast<std::int64_t>(output_format_.sample_rate_hz - interpolation_remainder)) +
               (next_sample * static_cast<std::int64_t>(interpolation_remainder))) /
              output_format_.sample_rate_hz;

          output_frame.samples[output_sample_index] = clamp_to_sample(static_cast<std::int32_t>(interpolated_sample));
        }
      }
    }

    if (enable_denoise_) {
      apply_wiener_filter(output_frame);
    }
  }

  auto AudioProcessor::input_frame_count() const -> std::uint32_t {
    return frames_for_period(input_format_.sample_rate_hz, publish_period_ms_);
  }

  auto AudioProcessor::mix_channel(const std::vector<std::int16_t>& input_samples, std::uint32_t input_frame_index,
                                   std::uint16_t output_channel_index) const -> std::int32_t {
    if (input_format_.channel_count == output_format_.channel_count) {
      const auto input_sample_index = (input_frame_index * input_format_.channel_count) + output_channel_index;
      return input_samples[input_sample_index];
    }

    const auto first_input_channel =
        static_cast<std::uint16_t>((output_channel_index * input_format_.channel_count) / output_format_.channel_count);
    auto last_input_channel = static_cast<std::uint16_t>(((output_channel_index + 1) * input_format_.channel_count) /
                                                         output_format_.channel_count);
    if (last_input_channel <= first_input_channel) {
      last_input_channel = static_cast<std::uint16_t>(first_input_channel + 1);
    }

    std::int32_t mixed_sample = 0;
    std::uint16_t mixed_channel_count = 0;

    for (auto input_channel = first_input_channel; input_channel < last_input_channel; ++input_channel) {
      const auto input_sample_index = (input_frame_index * input_format_.channel_count) + input_channel;
      mixed_sample += input_samples[input_sample_index];
      ++mixed_channel_count;
    }

    return mixed_sample / static_cast<std::int32_t>(mixed_channel_count);
  }

  void AudioProcessor::apply_wiener_filter(AudioFrame& output_frame) {
    const auto frame_count = output_frame.frame_count;
    const auto channel_count = output_frame.channel_count;
    if (frame_count == 0 || channel_count == 0) {
      return;
    }

    if (denoise_scratch_.size() != frame_count) {
      denoise_scratch_.resize(frame_count);
    }

    for (std::uint16_t channel_index = 0; channel_index < channel_count; ++channel_index) {
      for (std::uint32_t frame_index = 0; frame_index < frame_count; ++frame_index) {
        const auto sample_index = (frame_index * channel_count) + channel_index;
        denoise_scratch_[frame_index] = output_frame.samples[sample_index];
      }

      const auto noise_variance = estimate_noise_variance(denoise_scratch_);
      if (noise_variance <= 0) {
        continue;
      }

      for (std::uint32_t frame_index = 0; frame_index < frame_count; ++frame_index) {
        const auto stats = local_stats(denoise_scratch_, frame_index);
        const auto current_sample = static_cast<std::int32_t>(denoise_scratch_[frame_index]);
        std::int32_t filtered_sample = stats.mean;

        if (stats.variance > noise_variance) {
          const auto gain = ((stats.variance - noise_variance) * kWienerGainScale) / stats.variance;
          filtered_sample =
              stats.mean + static_cast<std::int32_t>((gain * (current_sample - stats.mean)) / kWienerGainScale);
        }

        const auto sample_index = (frame_index * channel_count) + channel_index;
        output_frame.samples[sample_index] = clamp_to_sample(filtered_sample);
      }
    }
  }

} // namespace signlang::audio_frontend
