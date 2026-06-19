#include "audio_ring_buffer.hpp"

#include "speech_asr_result.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace signlang::speech_asr {
  namespace {

    constexpr auto kInt16ToFloatScale = 1.0F / 32768.0F;

  } // namespace

  AudioRingBuffer::AudioRingBuffer(std::uint64_t capacity_samples) :
      samples_(static_cast<std::size_t>(capacity_samples)), start_sample_index_{0}, next_sample_index_{0},
      latest_audio_sequence_number_{0}, latest_audio_timestamp_ns_{0}, latest_audio_sample_rate_hz_{0},
      latest_audio_publish_period_ms_{0}, latest_audio_frame_count_{0}, latest_audio_channel_count_{0},
      latest_audio_bits_per_sample_{0} {
    if (capacity_samples == 0) {
      throw std::runtime_error("Audio ring buffer capacity must be greater than 0");
    }
  }

  auto AudioRingBuffer::push(const signlang::audio_frontend::AudioFrame& frame) -> bool {
    if (!accepts_metadata(frame)) {
      return false;
    }

    {
      const std::lock_guard<std::mutex> lock{mutex_};
      for (std::uint32_t frame_index = 0; frame_index < frame.frame_count; ++frame_index) {
        append_sample(static_cast<float>(frame.samples[frame_index]) * kInt16ToFloatScale);
      }

      latest_audio_sequence_number_ = frame.sequence_number;
      latest_audio_timestamp_ns_ = frame.timestamp_ns;
      latest_audio_sample_rate_hz_ = frame.sample_rate_hz;
      latest_audio_publish_period_ms_ = frame.publish_period_ms;
      latest_audio_frame_count_ = frame.frame_count;
      latest_audio_channel_count_ = frame.channel_count;
      latest_audio_bits_per_sample_ = frame.bits_per_sample;
    }

    samples_changed_.notify_all();
    return true;
  }

  auto AudioRingBuffer::wait_for_window(std::optional<std::uint64_t>& requested_start_sample_index,
                                        std::uint64_t window_sample_count, std::uint64_t hop_sample_count,
                                        const std::atomic_bool& should_stop, AudioWindow& output_window) -> bool {
    std::unique_lock<std::mutex> lock{mutex_};
    samples_changed_.wait(lock, [&] {
      return should_stop.load() || (next_sample_index_ - start_sample_index_) >= window_sample_count;
    });

    if (should_stop.load()) {
      return false;
    }

    auto window_start = requested_start_sample_index.value_or(next_sample_index_ - window_sample_count);
    window_start = align_to_available_window(window_start, hop_sample_count);

    while (!should_stop.load() && window_start + window_sample_count > next_sample_index_) {
      samples_changed_.wait(lock, [&] {
        return should_stop.load() || window_start + window_sample_count <= next_sample_index_;
      });
      window_start = align_to_available_window(window_start, hop_sample_count);
    }

    if (should_stop.load()) {
      return false;
    }

    if (output_window.samples.size() != window_sample_count) {
      output_window.samples.resize(static_cast<std::size_t>(window_sample_count));
    }

    const auto capacity = static_cast<std::uint64_t>(samples_.size());
    for (std::uint64_t offset = 0; offset < window_sample_count; ++offset) {
      output_window.samples[static_cast<std::size_t>(offset)] = samples_[(window_start + offset) % capacity];
    }

    output_window.start_sample_index = window_start;
    output_window.end_sample_index = window_start + window_sample_count;
    output_window.latest_audio_sequence_number = latest_audio_sequence_number_;
    output_window.latest_audio_timestamp_ns = latest_audio_timestamp_ns_;
    output_window.latest_audio_sample_rate_hz = latest_audio_sample_rate_hz_;
    output_window.latest_audio_publish_period_ms = latest_audio_publish_period_ms_;
    output_window.latest_audio_frame_count = latest_audio_frame_count_;
    output_window.latest_audio_channel_count = latest_audio_channel_count_;
    output_window.latest_audio_bits_per_sample = latest_audio_bits_per_sample_;
    requested_start_sample_index = window_start;
    return true;
  }

  void AudioRingBuffer::clear() {
    {
      const std::lock_guard<std::mutex> lock{mutex_};
      start_sample_index_ = next_sample_index_;
    }

    samples_changed_.notify_all();
  }

  void AudioRingBuffer::notify_stop() { samples_changed_.notify_all(); }

  auto AudioRingBuffer::accepts_metadata(const signlang::audio_frontend::AudioFrame& frame) -> bool {
    if (frame.sample_rate_hz != kWhisperSampleRateHz ||
        frame.bits_per_sample != signlang::audio_frontend::kBitsPerSample) {
      return false;
    }

    if (frame.publish_period_ms == 0 || frame.publish_period_ms > signlang::audio_frontend::kMaxPublishPeriodMs) {
      return false;
    }

    if (frame.channel_count != signlang::audio_frontend::kDefaultChannelCount || frame.frame_count == 0 ||
        frame.frame_count > signlang::audio_frontend::kMaxFramesPerPacket) {
      return false;
    }

    const auto expected_frame_count =
        static_cast<std::uint32_t>((static_cast<std::uint64_t>(kWhisperSampleRateHz) * frame.publish_period_ms) / 1000);
    if (frame.frame_count != expected_frame_count) {
      return false;
    }

    const auto sample_count = static_cast<std::uint64_t>(frame.frame_count) * frame.channel_count;
    return sample_count <= frame.samples.size();
  }

  void AudioRingBuffer::append_sample(float sample) {
    const auto capacity = static_cast<std::uint64_t>(samples_.size());
    samples_[next_sample_index_ % capacity] = sample;
    ++next_sample_index_;

    if (next_sample_index_ - start_sample_index_ > capacity) {
      start_sample_index_ = next_sample_index_ - capacity;
    }
  }

  auto AudioRingBuffer::align_to_available_window(std::uint64_t requested_start_sample_index,
                                                  std::uint64_t hop_sample_count) const -> std::uint64_t {
    if (requested_start_sample_index >= start_sample_index_) {
      return requested_start_sample_index;
    }

    if (hop_sample_count == 0) {
      return start_sample_index_;
    }

    const auto samples_behind = start_sample_index_ - requested_start_sample_index;
    const auto skipped_hops = (samples_behind + hop_sample_count - 1) / hop_sample_count;
    return requested_start_sample_index + (skipped_hops * hop_sample_count);
  }

  auto samples_for_window_ms(std::uint32_t sample_rate_hz, std::uint32_t window_ms) -> std::uint64_t {
    return (static_cast<std::uint64_t>(sample_rate_hz) * window_ms) / 1000;
  }

  auto hop_samples_for_overlap(std::uint64_t window_sample_count, double overlap_ratio) -> std::uint64_t {
    const auto hop_samples = static_cast<std::uint64_t>(
        std::llround(static_cast<double>(window_sample_count) * std::clamp(1.0 - overlap_ratio, 0.0, 1.0)));
    return std::max<std::uint64_t>(1, hop_samples);
  }

} // namespace signlang::speech_asr
