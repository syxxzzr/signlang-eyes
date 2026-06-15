#ifndef SIGNLANG_EYES_EDGEAI_SIGNLANG_DET_KEYPOINT_RING_BUFFER_HPP
#define SIGNLANG_EYES_EDGEAI_SIGNLANG_DET_KEYPOINT_RING_BUFFER_HPP

#include "signlang_result.hpp"

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

  auto get_window(std::uint32_t window_size)
    -> std::optional<std::vector<FeatureVector>>;

  auto size() const -> std::uint32_t;
  auto capacity() const -> std::uint32_t;

private:
  std::vector<FeatureVector> buffer_;
  std::uint32_t head_{0};
  std::uint32_t count_{0};
  std::uint32_t capacity_;
  mutable std::mutex mutex_;
};

auto compute_buffer_capacity(std::uint32_t sequence_length, float overlap_ratio)
  -> std::uint32_t;

} // namespace signlang::signlang_det

#endif // SIGNLANG_EYES_EDGEAI_SIGNLANG_DET_KEYPOINT_RING_BUFFER_HPP
