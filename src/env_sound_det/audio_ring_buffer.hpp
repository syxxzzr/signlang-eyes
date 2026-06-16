#ifndef SIGNLANG_EYES_EDGEAI_ENV_SOUND_DET_AUDIO_RING_BUFFER_HPP
#define SIGNLANG_EYES_EDGEAI_ENV_SOUND_DET_AUDIO_RING_BUFFER_HPP

#include "audio_frontend/audio_frame.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace signlang::env_sound_det {

  struct AudioWindow {
    std::vector<float> samples;
    std::uint64_t start_sample_index;
    std::uint64_t end_sample_index;
    std::uint64_t latest_audio_sequence_number;
    std::uint64_t latest_audio_timestamp_ns;
    std::uint32_t latest_audio_sample_rate_hz;
    std::uint32_t latest_audio_publish_period_ms;
    std::uint32_t latest_audio_frame_count;
    std::uint16_t latest_audio_channel_count;
    std::uint16_t latest_audio_bits_per_sample;
  };

  class AudioRingBuffer {
  public:
    explicit AudioRingBuffer(std::uint64_t capacity_samples);

    AudioRingBuffer(const AudioRingBuffer&) = delete;
    auto operator=(const AudioRingBuffer&) -> AudioRingBuffer& = delete;
    AudioRingBuffer(AudioRingBuffer&&) = delete;
    auto operator=(AudioRingBuffer&&) -> AudioRingBuffer& = delete;

    auto push(const signlang::audio_frontend::AudioFrame& frame) -> bool;
    auto wait_for_window(std::optional<std::uint64_t>& requested_start_sample_index,
                         std::uint64_t window_sample_count, std::uint64_t hop_sample_count,
                         const std::atomic_bool& should_stop, AudioWindow& output_window) -> bool;
    void notify_stop();

  private:
    static auto accepts_metadata(const signlang::audio_frontend::AudioFrame& frame) -> bool;
    void append_sample(float sample);
    auto align_to_available_window(std::uint64_t requested_start_sample_index, std::uint64_t hop_sample_count) const
        -> std::uint64_t;

    std::vector<float> samples_;
    mutable std::mutex mutex_;
    std::condition_variable samples_changed_;
    std::uint64_t start_sample_index_;
    std::uint64_t next_sample_index_;
    std::uint64_t latest_audio_sequence_number_;
    std::uint64_t latest_audio_timestamp_ns_;
    std::uint32_t latest_audio_sample_rate_hz_;
    std::uint32_t latest_audio_publish_period_ms_;
    std::uint32_t latest_audio_frame_count_;
    std::uint16_t latest_audio_channel_count_;
    std::uint16_t latest_audio_bits_per_sample_;
  };

  auto samples_for_window_ms(std::uint32_t sample_rate_hz, std::uint32_t window_ms) -> std::uint64_t;
  auto hop_samples_for_overlap(std::uint64_t window_sample_count, double overlap_ratio) -> std::uint64_t;

} // namespace signlang::env_sound_det

#endif // SIGNLANG_EYES_EDGEAI_ENV_SOUND_DET_AUDIO_RING_BUFFER_HPP
