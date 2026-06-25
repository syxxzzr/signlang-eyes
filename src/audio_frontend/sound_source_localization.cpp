#include "sound_source_localization.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

namespace signlang::audio_frontend {
  namespace {

    constexpr std::uint32_t kMaxAnalysisFrames = 2048;
    constexpr std::uint32_t kMaxTdoaWindowUs = 2000;
    constexpr std::uint32_t kMaxLagCapSamples = 256;
    constexpr double kMinMeanSquareEnergy = 64.0;
    constexpr float kMinUsefulCorrelation = 0.08F;

    auto clamp01(float value) -> float { return std::clamp(value, 0.0F, 1.0F); }

    auto sample_at(const std::vector<std::int16_t>& samples, std::uint32_t frame_index, std::uint16_t channel_count,
                   std::uint16_t channel_index) -> double {
      const auto sample_index = (static_cast<std::size_t>(frame_index) * channel_count) + channel_index;
      return static_cast<double>(samples[sample_index]);
    }

    void normalize_scores(std::array<float, kMaxChannelCount>& scores, std::uint16_t channel_count) {
      const auto sum = std::accumulate(scores.begin(), scores.begin() + channel_count, 0.0F);
      if (sum <= std::numeric_limits<float>::epsilon()) {
        const auto uniform_score = 1.0F / static_cast<float>(channel_count);
        std::fill(scores.begin(), scores.begin() + channel_count, uniform_score);
        return;
      }

      for (std::uint16_t channel_index = 0; channel_index < channel_count; ++channel_index) {
        scores[channel_index] /= sum;
      }
    }

  } // namespace

  SoundSourceLocalizer::SoundSourceLocalizer(float tdoa_weight, float rms_weight) :
      tdoa_weight_{tdoa_weight}, rms_weight_{rms_weight} {}

  auto SoundSourceLocalizer::estimate(const std::vector<std::int16_t>& interleaved_samples, AudioFormat format,
                                      std::uint64_t sequence_number, std::uint64_t timestamp_ns) const
      -> SoundSourceLocalizationResult {
    SoundSourceLocalizationResult result{
        .sequence_number = sequence_number,
        .timestamp_ns = timestamp_ns,
        .sample_rate_hz = format.sample_rate_hz,
        .frame_count = 0,
        .channel_count = format.channel_count,
        .strongest_channel = 0,
        .confidence = 0.0F,
        .valid = false,
        .proximity = {},
        .rms = {},
        .tdoa_score = {},
    };

    if (format.channel_count == 0 || format.channel_count > kMaxChannelCount ||
        interleaved_samples.size() < format.channel_count) {
      return result;
    }

    const auto captured_frames =
        static_cast<std::uint32_t>(interleaved_samples.size() / static_cast<std::size_t>(format.channel_count));
    const auto analysis_frames = std::min(captured_frames, kMaxAnalysisFrames);
    const auto first_frame = captured_frames - analysis_frames;
    result.frame_count = analysis_frames;

    std::array<double, kMaxChannelCount> mean_square{};
    double total_mean_square = 0.0;
    for (std::uint16_t channel_index = 0; channel_index < format.channel_count; ++channel_index) {
      double sum_square = 0.0;
      for (std::uint32_t offset = 0; offset < analysis_frames; ++offset) {
        const auto sample = sample_at(interleaved_samples, first_frame + offset, format.channel_count, channel_index);
        sum_square += sample * sample;
      }

      mean_square[channel_index] = analysis_frames == 0 ? 0.0 : sum_square / analysis_frames;
      result.rms[channel_index] = static_cast<float>(std::sqrt(mean_square[channel_index]));
      result.proximity[channel_index] = static_cast<float>(mean_square[channel_index]);
      total_mean_square += mean_square[channel_index];
    }

    if (analysis_frames == 0 || total_mean_square < kMinMeanSquareEnergy) {
      return result;
    }

    normalize_scores(result.proximity, format.channel_count);
    result.strongest_channel = static_cast<std::uint16_t>(
        std::distance(result.proximity.begin(),
                      std::max_element(result.proximity.begin(), result.proximity.begin() + format.channel_count)));

    if (format.channel_count == 1) {
      result.tdoa_score[0] = 1.0F;
      result.proximity[0] = 1.0F;
      result.confidence = 1.0F;
      result.valid = true;
      return result;
    }

    const auto lag_from_rate =
        static_cast<std::uint32_t>((static_cast<std::uint64_t>(format.sample_rate_hz) * kMaxTdoaWindowUs) / 1000000);
    const auto max_lag_samples = std::min({lag_from_rate, kMaxLagCapSamples, analysis_frames - 1});

    float correlation_sum = 0.0F;
    std::uint32_t correlation_count = 0;
    for (std::uint16_t channel_a = 0; channel_a < format.channel_count; ++channel_a) {
      for (std::uint16_t channel_b = static_cast<std::uint16_t>(channel_a + 1); channel_b < format.channel_count;
           ++channel_b) {
        const auto lag = estimate_lag(interleaved_samples, first_frame, analysis_frames, format.channel_count,
                                      channel_a, channel_b, max_lag_samples);
        const auto strength = clamp01(lag.correlation);
        if (strength < kMinUsefulCorrelation) {
          continue;
        }

        if (lag.lag_samples > 0) {
          result.tdoa_score[channel_a] += strength;
        } else if (lag.lag_samples < 0) {
          result.tdoa_score[channel_b] += strength;
        } else {
          result.tdoa_score[channel_a] += strength * 0.5F;
          result.tdoa_score[channel_b] += strength * 0.5F;
        }

        correlation_sum += strength;
        ++correlation_count;
      }
    }

    normalize_scores(result.tdoa_score, format.channel_count);

    for (std::uint16_t channel_index = 0; channel_index < format.channel_count; ++channel_index) {
      result.proximity[channel_index] =
          (tdoa_weight_ * result.tdoa_score[channel_index]) + (rms_weight_ * result.proximity[channel_index]);
    }
    normalize_scores(result.proximity, format.channel_count);

    result.strongest_channel = static_cast<std::uint16_t>(
        std::distance(result.proximity.begin(),
                      std::max_element(result.proximity.begin(), result.proximity.begin() + format.channel_count)));

    const auto average_correlation =
        correlation_count == 0 ? 0.0F : correlation_sum / static_cast<float>(correlation_count);
    const auto strongest_score = result.proximity[result.strongest_channel];
    result.confidence = clamp01((0.65F * average_correlation) + (0.35F * strongest_score));
    result.valid = result.confidence > 0.0F;
    return result;
  }

  auto SoundSourceLocalizer::estimate_lag(const std::vector<std::int16_t>& interleaved_samples,
                                          std::uint32_t first_frame, std::uint32_t frame_count,
                                          std::uint16_t channel_count, std::uint16_t channel_a, std::uint16_t channel_b,
                                          std::uint32_t max_lag_samples) -> LagEstimate {
    LagEstimate best{.lag_samples = 0, .correlation = -1.0F};
    const auto max_lag = static_cast<int>(max_lag_samples);

    for (int lag = -max_lag; lag <= max_lag; ++lag) {
      const auto start_a = lag < 0 ? static_cast<std::uint32_t>(-lag) : 0U;
      const auto start_b = lag > 0 ? static_cast<std::uint32_t>(lag) : 0U;
      const auto usable_frames = frame_count - std::max(start_a, start_b);
      if (usable_frames == 0) {
        continue;
      }

      double cross = 0.0;
      double square_a = 0.0;
      double square_b = 0.0;
      for (std::uint32_t offset = 0; offset < usable_frames; ++offset) {
        const auto sample_a = sample_at(interleaved_samples, first_frame + start_a + offset, channel_count, channel_a);
        const auto sample_b = sample_at(interleaved_samples, first_frame + start_b + offset, channel_count, channel_b);
        cross += sample_a * sample_b;
        square_a += sample_a * sample_a;
        square_b += sample_b * sample_b;
      }

      if (square_a <= 0.0 || square_b <= 0.0) {
        continue;
      }

      const auto correlation = static_cast<float>(cross / std::sqrt(square_a * square_b));
      if (correlation > best.correlation) {
        best = LagEstimate{.lag_samples = lag, .correlation = correlation};
      }
    }

    best.correlation = std::max(0.0F, best.correlation);
    return best;
  }

} // namespace signlang::audio_frontend
