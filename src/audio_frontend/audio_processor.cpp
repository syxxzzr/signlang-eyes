#include "audio_processor.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace signlang::audio_frontend {
  namespace {

    auto frames_for_period(std::uint32_t sample_rate_hz, std::uint32_t publish_period_ms) -> std::uint32_t {
      return static_cast<std::uint32_t>((static_cast<std::uint64_t>(sample_rate_hz) * publish_period_ms) / 1000);
    }

    auto clamp_to_sample(std::int32_t value) -> std::int16_t {
      const auto clamped_value = std::clamp(value, static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min()),
                                            static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::max()));
      return static_cast<std::int16_t>(clamped_value);
    }

  } // namespace

  AudioProcessor::AudioProcessor(AudioFormat input_format, AudioFormat output_format, std::uint32_t publish_period_ms) :
      input_format_{input_format}, output_format_{output_format}, publish_period_ms_{publish_period_ms} {
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

} // namespace signlang::audio_frontend
