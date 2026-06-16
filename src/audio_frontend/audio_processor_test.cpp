#include "audio_processor.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

  void require_equal(std::int16_t actual, std::int16_t expected, const char* context) {
    if (actual != expected) {
      throw std::runtime_error(std::string(context) + ": expected " + std::to_string(expected) + ", got " +
                               std::to_string(actual));
    }
  }

  auto make_output_frame(std::uint32_t sample_rate_hz, std::uint32_t publish_period_ms, std::uint32_t frame_count,
                         std::uint16_t channel_count) -> signlang::audio_frontend::AudioFrame {
    signlang::audio_frontend::AudioFrame frame{};
    frame.sample_rate_hz = sample_rate_hz;
    frame.publish_period_ms = publish_period_ms;
    frame.frame_count = frame_count;
    frame.channel_count = channel_count;
    frame.bits_per_sample = signlang::audio_frontend::kBitsPerSample;
    return frame;
  }

  void test_wiener_denoise_matches_characterized_output() {
    constexpr std::uint32_t kSampleRateHz = 1000;
    constexpr std::uint32_t kPublishPeriodMs = 10;
    constexpr std::uint32_t kFrameCount = 10;
    constexpr std::uint16_t kChannelCount = 1;

    const signlang::audio_frontend::AudioFormat format{
        .sample_rate_hz = kSampleRateHz,
        .channel_count = kChannelCount,
    };
    const std::vector<std::int16_t> input_samples{0, 100, 0, 100, 0, 10, 10, 10, 10, 10};
    const std::array<std::int16_t, kFrameCount> expected_samples{21, 72, 24, 64, 23, 26, 8, 10, 10, 10};

    signlang::audio_frontend::AudioProcessor processor{format, format, kPublishPeriodMs, true};
    auto output_frame = make_output_frame(kSampleRateHz, kPublishPeriodMs, kFrameCount, kChannelCount);

    processor.process(input_samples, output_frame);

    for (std::uint32_t sample_index = 0; sample_index < kFrameCount; ++sample_index) {
      require_equal(output_frame.samples[sample_index], expected_samples[sample_index], "denoised sample");
    }
  }

} // namespace

auto main() -> int {
  try {
    test_wiener_denoise_matches_characterized_output();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
