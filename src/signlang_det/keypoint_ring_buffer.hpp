#ifndef SIGNLANG_EYES_SIGNLANG_DET_KEYPOINT_RING_BUFFER_HPP
#define SIGNLANG_EYES_SIGNLANG_DET_KEYPOINT_RING_BUFFER_HPP

#include "signlang_result.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace signlang::signlang_det {

class KeypointRingBuffer {
public:
  explicit KeypointRingBuffer(std::uint32_t capacity);

  KeypointRingBuffer(const KeypointRingBuffer&) = delete;
  auto operator=(const KeypointRingBuffer&) -> KeypointRingBuffer& = delete;
  KeypointRingBuffer(KeypointRingBuffer&&) = delete;
  auto operator=(KeypointRingBuffer&&) -> KeypointRingBuffer& = delete;

  void push(const FeatureVector& feature);

  auto wait_for_window(std::uint32_t window_size, std::uint64_t min_end_sequence,
                       const std::atomic_bool& should_stop)
    -> std::optional<std::vector<FeatureVector>>;

  void clear();
  void notify_stop();

  auto size() const -> std::uint32_t;
  auto capacity() const -> std::uint32_t;

private:
  std::vector<FeatureVector> buffer_;
  std::uint32_t head_{0};
  std::uint32_t count_{0};
  std::uint32_t capacity_;
  mutable std::mutex mutex_;
  std::condition_variable changed_;

  auto latest_window_end_sequence_locked(std::uint32_t window_size) const -> std::optional<std::uint64_t>;
  auto copy_latest_window_locked(std::uint32_t window_size) const -> std::vector<FeatureVector>;
};

auto compute_buffer_capacity(std::uint32_t sequence_length, float overlap_ratio)
  -> std::uint32_t;

} // namespace signlang::signlang_det

#endif // SIGNLANG_EYES_SIGNLANG_DET_KEYPOINT_RING_BUFFER_HPP
