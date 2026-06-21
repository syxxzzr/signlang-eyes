#include "common/audio_ring_buffer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace signlang::common {
  namespace {

    constexpr auto kInt16ToFloatScale = 1.0F / 32768.0F;

  } // namespace

  AudioRingBuffer::AudioRingBuffer(std::uint64_t capacity_samples, std::uint32_t expected_sample_rate_hz) :
      expected_sample_rate_hz_{expected_sample_rate_hz},
      samples_(static_cast<std::size_t>(capacity_samples)), start_sample_index_{0}, next_sample_index_{0}, wake_sequence_{0},
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

    const auto capacity = static_cast<std::uint64_t>(samples_.size());
    auto next_sample_index = next_sample_index_.load(std::memory_order_relaxed);
    for (std::uint32_t frame_index = 0; frame_index < frame.frame_count; ++frame_index) {
      samples_[next_sample_index % capacity].store(static_cast<float>(frame.samples[frame_index]) * kInt16ToFloatScale,
                                                   std::memory_order_relaxed);
      ++next_sample_index;
    }

    const auto start_sample_index = next_sample_index > capacity ? next_sample_index - capacity : std::uint64_t{0};
    latest_audio_sequence_number_.store(frame.sequence_number, std::memory_order_relaxed);
    latest_audio_timestamp_ns_.store(frame.timestamp_ns, std::memory_order_relaxed);
    latest_audio_sample_rate_hz_.store(frame.sample_rate_hz, std::memory_order_relaxed);
    latest_audio_publish_period_ms_.store(frame.publish_period_ms, std::memory_order_relaxed);
    latest_audio_frame_count_.store(frame.frame_count, std::memory_order_relaxed);
    latest_audio_channel_count_.store(frame.channel_count, std::memory_order_relaxed);
    latest_audio_bits_per_sample_.store(frame.bits_per_sample, std::memory_order_relaxed);
    start_sample_index_.store(start_sample_index, std::memory_order_relaxed);
    next_sample_index_.store(next_sample_index, std::memory_order_release);
    wake_sequence_.fetch_add(1, std::memory_order_release);
    wake_sequence_.notify_all();
    return true;
  }

  auto AudioRingBuffer::wait_for_window(std::optional<std::uint64_t>& requested_start_sample_index,
                                        std::uint64_t window_sample_count, std::uint64_t hop_sample_count,
                                        const std::atomic_bool& should_stop, AudioWindow& output_window) -> bool {
    if (output_window.samples.size() != window_sample_count) {
      output_window.samples.resize(static_cast<std::size_t>(window_sample_count));
    }

    const auto capacity = static_cast<std::uint64_t>(samples_.size());
    while (!should_stop.load(std::memory_order_relaxed)) {
      const auto observed_wake_sequence = wake_sequence_.load(std::memory_order_acquire);
      auto start_sample_index = start_sample_index_.load(std::memory_order_acquire);
      auto next_sample_index = next_sample_index_.load(std::memory_order_acquire);
      if (next_sample_index - start_sample_index < window_sample_count) {
        wait_for_samples(observed_wake_sequence, should_stop);
        continue;
      }

      auto window_start = requested_start_sample_index.value_or(next_sample_index - window_sample_count);
      window_start = align_to_available_window(window_start, start_sample_index, hop_sample_count);
      if (window_start + window_sample_count > next_sample_index) {
        wait_for_samples(observed_wake_sequence, should_stop);
        continue;
      }

      for (std::uint64_t offset = 0; offset < window_sample_count; ++offset) {
        output_window.samples[static_cast<std::size_t>(offset)] =
            samples_[(window_start + offset) % capacity].load(std::memory_order_relaxed);
      }

      start_sample_index = start_sample_index_.load(std::memory_order_acquire);
      if (window_start < start_sample_index) {
        requested_start_sample_index = align_to_available_window(window_start, start_sample_index, hop_sample_count);
        continue;
      }

      output_window.start_sample_index = window_start;
      output_window.end_sample_index = window_start + window_sample_count;
      output_window.latest_audio_sequence_number = latest_audio_sequence_number_.load(std::memory_order_relaxed);
      output_window.latest_audio_timestamp_ns = latest_audio_timestamp_ns_.load(std::memory_order_relaxed);
      output_window.latest_audio_sample_rate_hz = latest_audio_sample_rate_hz_.load(std::memory_order_relaxed);
      output_window.latest_audio_publish_period_ms = latest_audio_publish_period_ms_.load(std::memory_order_relaxed);
      output_window.latest_audio_frame_count = latest_audio_frame_count_.load(std::memory_order_relaxed);
      output_window.latest_audio_channel_count = latest_audio_channel_count_.load(std::memory_order_relaxed);
      output_window.latest_audio_bits_per_sample = latest_audio_bits_per_sample_.load(std::memory_order_relaxed);
      requested_start_sample_index = window_start;
      return true;
    }

    return false;
  }

  void AudioRingBuffer::clear() {
    start_sample_index_.store(next_sample_index_.load(std::memory_order_acquire), std::memory_order_release);
    wake_sequence_.fetch_add(1, std::memory_order_release);
    wake_sequence_.notify_all();
  }

  void AudioRingBuffer::notify_stop() {
    wake_sequence_.fetch_add(1, std::memory_order_release);
    wake_sequence_.notify_all();
  }

  auto AudioRingBuffer::accepts_metadata(const signlang::audio_frontend::AudioFrame& frame) const -> bool {
    if (frame.sample_rate_hz != expected_sample_rate_hz_ ||
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
        static_cast<std::uint32_t>((static_cast<std::uint64_t>(expected_sample_rate_hz_) * frame.publish_period_ms) / 1000);
    if (frame.frame_count != expected_frame_count) {
      return false;
    }

    const auto sample_count = static_cast<std::uint64_t>(frame.frame_count) * frame.channel_count;
    return sample_count <= frame.samples.size();
  }

  auto AudioRingBuffer::align_to_available_window(std::uint64_t requested_start_sample_index,
                                                  std::uint64_t available_start_sample_index,
                                                  std::uint64_t hop_sample_count) -> std::uint64_t {
    if (requested_start_sample_index >= available_start_sample_index) {
      return requested_start_sample_index;
    }

    if (hop_sample_count == 0) {
      return available_start_sample_index;
    }

    const auto samples_behind = available_start_sample_index - requested_start_sample_index;
    const auto skipped_hops = (samples_behind + hop_sample_count - 1) / hop_sample_count;
    return requested_start_sample_index + (skipped_hops * hop_sample_count);
  }

  void AudioRingBuffer::wait_for_samples(std::uint64_t observed_wake_sequence,
                                         const std::atomic_bool& should_stop) const {
    if (!should_stop.load(std::memory_order_relaxed)) {
      wake_sequence_.wait(observed_wake_sequence, std::memory_order_acquire);
    }
  }

  auto samples_for_window_ms(std::uint32_t sample_rate_hz, std::uint32_t window_ms) -> std::uint64_t {
    return (static_cast<std::uint64_t>(sample_rate_hz) * window_ms) / 1000;
  }

  auto hop_samples_for_overlap(std::uint64_t window_sample_count, double overlap_ratio) -> std::uint64_t {
    const auto hop_samples = static_cast<std::uint64_t>(
        std::llround(static_cast<double>(window_sample_count) * std::clamp(1.0 - overlap_ratio, 0.0, 1.0)));
    return std::max<std::uint64_t>(1, hop_samples);
  }

} // namespace signlang::common
