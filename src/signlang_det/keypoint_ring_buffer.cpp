#include "keypoint_ring_buffer.hpp"

#include <algorithm>
#include <stdexcept>

namespace signlang::signlang_det {
  namespace {

    auto compute_buffer_capacity_impl(std::uint32_t sequence_length, float overlap_ratio)
      -> std::uint32_t
    {
      if (sequence_length == 0) {
        throw std::invalid_argument("sequence_length must be > 0");
      }
      if (overlap_ratio < 0.0f || overlap_ratio >= 1.0f) {
        throw std::invalid_argument("overlap_ratio must be in [0.0, 1.0)");
      }

      const auto hop_size = static_cast<std::uint32_t>(
        sequence_length * (1.0f - overlap_ratio));
      return std::max(sequence_length * 2, sequence_length + hop_size);
    }

  } // namespace

  auto compute_buffer_capacity(std::uint32_t sequence_length, float overlap_ratio)
    -> std::uint32_t
  {
    return compute_buffer_capacity_impl(sequence_length, overlap_ratio);
  }

KeypointRingBuffer::KeypointRingBuffer(std::uint32_t capacity)
  : buffer_(capacity), capacity_(capacity) {
  if (capacity == 0) {
    throw std::invalid_argument("Ring buffer capacity must be > 0");
  }
}

void KeypointRingBuffer::push(const FeatureVector& feature) {
  {
    std::lock_guard lock(mutex_);

    buffer_[head_] = feature;
    head_ = (head_ + 1) % capacity_;

    if (count_ < capacity_) {
      ++count_;
    }
  }

  changed_.notify_all();
}

auto KeypointRingBuffer::wait_for_window(std::uint32_t window_size, std::uint64_t min_end_sequence,
                                         const std::atomic_bool& should_stop)
  -> std::optional<std::vector<FeatureVector>>
{
  std::unique_lock lock(mutex_);
  changed_.wait(lock, [&] {
    const auto latest_end_sequence = latest_window_end_sequence_locked(window_size);
    return should_stop.load() ||
           (latest_end_sequence.has_value() && latest_end_sequence.value() >= min_end_sequence);
  });

  if (should_stop.load()) {
    return std::nullopt;
  }

  const auto latest_end_sequence = latest_window_end_sequence_locked(window_size);
  if (!latest_end_sequence.has_value() || latest_end_sequence.value() < min_end_sequence) {
    return std::nullopt;
  }

  return copy_latest_window_locked(window_size);
}

auto KeypointRingBuffer::latest_window_end_sequence_locked(std::uint32_t window_size) const
  -> std::optional<std::uint64_t>
{
  if (count_ < window_size || window_size == 0) {
    return std::nullopt;
  }

  const auto latest_index = (head_ + capacity_ - 1) % capacity_;
  return buffer_[latest_index].source_sequence_number;
}

auto KeypointRingBuffer::copy_latest_window_locked(std::uint32_t window_size) const
  -> std::vector<FeatureVector>
{
  auto window = std::vector<FeatureVector>{};
  window.reserve(window_size);

  const auto start_index = (head_ + capacity_ - window_size) % capacity_;
  for (std::uint32_t i = 0; i < window_size; ++i) {
    const auto index = (start_index + i) % capacity_;
    window.push_back(buffer_[index]);
  }

  return window;
}

auto KeypointRingBuffer::size() const -> std::uint32_t {
  std::lock_guard lock(mutex_);
  return count_;
}

void KeypointRingBuffer::clear() {
  {
    std::lock_guard lock(mutex_);
    head_ = 0;
    count_ = 0;
  }

  changed_.notify_all();
}

void KeypointRingBuffer::notify_stop() {
  changed_.notify_all();
}

auto KeypointRingBuffer::capacity() const -> std::uint32_t {
  return capacity_;
}

} // namespace signlang::signlang_det
