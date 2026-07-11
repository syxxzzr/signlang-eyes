#include "gesture_pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace signlang::signlang_det {

  GestureSegmenter::GestureSegmenter(SegmenterOptions options) : options_{options} {
    if (options_.pre_roll_frames == 0 || options_.start_confirmation_frames == 0 ||
        options_.end_confirmation_frames == 0 || options_.minimum_frames == 0 ||
        options_.maximum_frames < options_.minimum_frames || options_.maximum_frames > kMaxCapturedFrames ||
        options_.post_roll_frames > options_.end_confirmation_frames || options_.motion_ema_alpha <= 0.0F ||
        options_.motion_ema_alpha > 1.0F) {
      throw std::invalid_argument("invalid gesture segmenter options");
    }
  }

  auto GestureSegmenter::has_hand(const FeatureVector& frame) -> bool {
    return frame.hands[0].present || frame.hands[1].present;
  }

  void GestureSegmenter::remember_pre_roll(const FeatureVector& frame) {
    pre_roll_.push_back(frame);
    while (pre_roll_.size() > options_.pre_roll_frames) pre_roll_.pop_front();
  }

  void GestureSegmenter::enter_idle() {
    state_ = State::Idle;
    active_frames_.clear();
    pre_roll_.clear();
    confirmation_count_ = 0;
    quiet_count_ = 0;
    arming_static_ = false;
    static_action_ = false;
  }

  void GestureSegmenter::reset() {
    enter_idle();
    smoothed_motion_ = 0.0F;
    previous_present_ = false;
    cooldown_started_ns_ = 0;
  }

  auto GestureSegmenter::close_segment(bool forced) -> SegmentUpdate {
    auto frames = std::move(active_frames_);
    active_frames_.clear();
    state_ = State::Cooldown;
    quiet_count_ = 0;
    cooldown_started_ns_ = frames.empty() ? 0 : frames.back().timestamp_ns;
    if (frames.size() < options_.minimum_frames) return {SegmentEvent::DroppedTooShort, std::nullopt};

    auto present_count = std::uint32_t{0};
    auto confidence_sum = 0.0F;
    auto peak_motion = 0.0F;
    for (const auto& frame : frames) {
      if (has_hand(frame)) {
        ++present_count;
        confidence_sum += frame.mean_confidence;
      }
      peak_motion = std::max(peak_motion, frame.motion_score);
    }
    const auto ratio = static_cast<float>(present_count) / static_cast<float>(frames.size());
    const auto confidence = present_count == 0 ? 0.0F : confidence_sum / static_cast<float>(present_count);
    const auto score = std::clamp(ratio * confidence, 0.0F, 1.0F);
    return {SegmentEvent::Emitted,
            GestureSegment{std::move(frames), SegmentQuality{ratio, confidence, peak_motion, score}, forced}};
  }

  auto GestureSegmenter::push(const FeatureVector& frame) -> SegmentUpdate {
    if (!frame.sequence_continuous) {
      const auto interrupted = state_ == State::Arming || state_ == State::Active || state_ == State::Ending;
      reset();
      remember_pre_roll(frame);
      previous_present_ = has_hand(frame);
      return {interrupted ? SegmentEvent::DroppedSequenceGap : SegmentEvent::None, std::nullopt};
    }
    const auto present = has_hand(frame);
    smoothed_motion_ += options_.motion_ema_alpha * (frame.motion_score - smoothed_motion_);
    const auto motion_start = smoothed_motion_ >= options_.start_motion_threshold;
    const auto static_start = present && !previous_present_ && frame.mean_confidence >= options_.static_presence_threshold;
    previous_present_ = present;

    if (state_ == State::Cooldown) {
      quiet_count_ = smoothed_motion_ <= options_.end_motion_threshold ? quiet_count_ + 1 : 0;
      const auto elapsed = frame.timestamp_ns >= cooldown_started_ns_ ? frame.timestamp_ns - cooldown_started_ns_ : 0;
      if (quiet_count_ >= options_.cooldown_quiet_frames ||
          elapsed >= static_cast<std::uint64_t>(options_.cooldown_ms) * 1'000'000U) enter_idle();
      return {};
    }
    if (state_ == State::Idle) {
      remember_pre_roll(frame);
      if (motion_start || static_start) {
        state_ = State::Arming;
        confirmation_count_ = 1;
        arming_static_ = static_start && !motion_start;
        active_frames_.assign(pre_roll_.begin(), pre_roll_.end());
      }
      return {};
    }
    if (state_ == State::Arming) {
      active_frames_.push_back(frame);
      if (arming_static_ ? present : motion_start) {
        if (++confirmation_count_ >= options_.start_confirmation_frames) {
          state_ = State::Active;
          static_action_ = arming_static_;
        }
      } else {
        enter_idle();
        remember_pre_roll(frame);
      }
      return {};
    }

    active_frames_.push_back(frame);
    if (active_frames_.size() >= options_.maximum_frames) return close_segment(true);
    const auto ending = static_action_ ? !present : (smoothed_motion_ <= options_.end_motion_threshold || !present);
    if (state_ == State::Active) {
      if (ending) {
        state_ = State::Ending;
        confirmation_count_ = 1;
      }
      return {};
    }
    if (!ending) {
      state_ = State::Active;
      confirmation_count_ = 0;
      return {};
    }
    if (++confirmation_count_ < options_.end_confirmation_frames) return {};
    const auto discard = options_.end_confirmation_frames - options_.post_roll_frames;
    if (discard < active_frames_.size()) {
      active_frames_.erase(active_frames_.end() - static_cast<std::ptrdiff_t>(discard), active_frames_.end());
    }
    return close_segment(false);
  }

  SegmentPreprocessor::SegmentPreprocessor(PreprocessingOptions options) : options_{options} {
    if (options_.minimum_frames == 0 || options_.minimum_frames > kMaxSequenceLength ||
        options_.minimum_present_frame_ratio < 0.0F || options_.minimum_present_frame_ratio > 1.0F ||
        options_.minimum_mean_confidence < 0.0F || options_.minimum_mean_confidence > 1.0F ||
        options_.minimum_quality < 0.0F || options_.minimum_quality > 1.0F) {
      throw std::invalid_argument("invalid segment preprocessing options");
    }
  }

  auto SegmentPreprocessor::has_hand(const FeatureVector& frame) -> bool {
    return frame.hands[0].present || frame.hands[1].present;
  }

  auto SegmentPreprocessor::evaluate(std::vector<FeatureVector> frames, bool forced) const -> GestureSegment {
    if (frames.empty()) throw std::runtime_error("gesture segment is empty");
    auto present_count = std::uint32_t{0};
    auto confidence_sum = 0.0F;
    auto peak_motion = 0.0F;
    for (const auto& frame : frames) {
      if (!frame.sequence_continuous) throw std::runtime_error("gesture segment contains a sequence gap");
      if (has_hand(frame)) {
        ++present_count;
        confidence_sum += frame.mean_confidence;
      }
      peak_motion = std::max(peak_motion, frame.motion_score);
      for (const auto& hand : frame.hands) for (const auto& point : hand.features) {
        if (!std::isfinite(point.normalized_x) || !std::isfinite(point.normalized_y) ||
            !std::isfinite(point.normalized_z) || !std::isfinite(point.velocity_magnitude)) {
          throw std::runtime_error("gesture segment contains non-finite features");
        }
      }
    }
    const auto ratio = static_cast<float>(present_count) / static_cast<float>(frames.size());
    const auto confidence = present_count == 0 ? 0.0F : confidence_sum / static_cast<float>(present_count);
    const auto score = std::clamp(ratio * confidence, 0.0F, 1.0F);
    if (frames.size() < options_.minimum_frames || ratio < options_.minimum_present_frame_ratio ||
        confidence < options_.minimum_mean_confidence || score < options_.minimum_quality) {
      throw std::runtime_error("gesture segment does not meet quality requirements");
    }
    return {std::move(frames), SegmentQuality{ratio, confidence, peak_motion, score}, forced};
  }

  auto SegmentPreprocessor::resample(const std::vector<FeatureVector>& frames) -> std::vector<FeatureVector> {
    auto output = std::vector<FeatureVector>(kMaxSequenceLength);
    for (std::uint32_t out = 0; out < kMaxSequenceLength; ++out) {
      const auto source = static_cast<float>(out) * static_cast<float>(frames.size() - 1U) /
                          static_cast<float>(kMaxSequenceLength - 1U);
      const auto lower = static_cast<std::size_t>(std::floor(source));
      const auto upper = std::min(lower + 1U, frames.size() - 1U);
      const auto weight = source - static_cast<float>(lower);
      const auto nearest = static_cast<std::size_t>(std::lround(source));
      auto& target = output[out];
      target.source_sequence_number = frames[nearest].source_sequence_number;
      target.timestamp_ns = frames[nearest].timestamp_ns;
      target.mean_confidence = frames[nearest].mean_confidence;
      target.sequence_continuous = true;
      for (std::uint32_t hand = 0; hand < kMaxHandCount; ++hand) {
        target.hands[hand].present = frames[nearest].hands[hand].present;
        target.hands[hand].confidence = frames[nearest].hands[hand].confidence;
        for (std::uint32_t point = 0; point < handpose_det::kHandPoseKeypointCount; ++point) {
          auto& destination = target.hands[hand].features[point];
          if (!target.hands[hand].present) {
            destination = {};
            continue;
          }
          const auto& nearest_hand = frames[nearest].hands[hand];
          const auto& left = frames[lower].hands[hand].present ? frames[lower].hands[hand].features[point]
                                                               : nearest_hand.features[point];
          const auto& right = frames[upper].hands[hand].present ? frames[upper].hands[hand].features[point]
                                                                : nearest_hand.features[point];
          destination.normalized_x = left.normalized_x + (right.normalized_x - left.normalized_x) * weight;
          destination.normalized_y = left.normalized_y + (right.normalized_y - left.normalized_y) * weight;
          destination.normalized_z = left.normalized_z + (right.normalized_z - left.normalized_z) * weight;
        }
      }
    }
    for (std::size_t index = 1; index < output.size(); ++index) {
      for (std::uint32_t hand = 0; hand < kMaxHandCount; ++hand) {
        if (!output[index].hands[hand].present || !output[index - 1U].hands[hand].present) continue;
        for (std::uint32_t point = 0; point < handpose_det::kHandPoseKeypointCount; ++point) {
          auto& current = output[index].hands[hand].features[point];
          const auto& previous = output[index - 1U].hands[hand].features[point];
          const auto dx = current.normalized_x - previous.normalized_x;
          const auto dy = current.normalized_y - previous.normalized_y;
          const auto dz = current.normalized_z - previous.normalized_z;
          current.velocity_magnitude = std::sqrt(dx * dx + dy * dy + dz * dz);
          output[index].motion_score += current.velocity_magnitude;
        }
      }
      output[index].motion_score /= static_cast<float>(kMaxHandCount * handpose_det::kHandPoseKeypointCount);
    }
    return output;
  }

  void SegmentPreprocessor::write_frame(const FeatureVector& frame, float* destination) {
    auto offset = std::size_t{0};
    for (const auto& hand : frame.hands) for (const auto& point : hand.features) {
      destination[offset++] = point.normalized_x;
      destination[offset++] = point.normalized_y;
      destination[offset++] = point.normalized_z;
      destination[offset++] = point.velocity_magnitude;
    }
  }

  auto SegmentPreprocessor::prepare(const GestureSegment& segment) const -> PackedSegment {
    auto begin = segment.frames.begin();
    auto end = segment.frames.end();
    while (begin != end && !has_hand(*begin)) ++begin;
    while (begin != end && !has_hand(*(end - 1))) --end;
    if (begin == end) throw std::runtime_error("gesture segment has no hand frames");
    auto frames = std::vector<FeatureVector>{begin, end};
    if (frames.size() < options_.minimum_frames || frames.size() > kMaxCapturedFrames) {
      throw std::runtime_error("gesture segment length is outside supported range");
    }
    auto packed = PackedSegment{};
    packed.landmarks.fill(kPaddingValue);
    packed.original_length = static_cast<std::uint32_t>(frames.size());
    packed.start_sequence = frames.front().source_sequence_number;
    packed.end_sequence = frames.back().source_sequence_number;
    packed.start_timestamp_ns = frames.front().timestamp_ns;
    packed.end_timestamp_ns = frames.back().timestamp_ns;
    packed.quality = segment.quality;
    packed.forced_max_length = segment.forced_max_length;
    if (frames.size() > kMaxSequenceLength) frames = resample(frames);
    packed.valid_length = static_cast<std::uint32_t>(frames.size());
    for (std::size_t index = 0; index < frames.size(); ++index) {
      write_frame(frames[index], packed.landmarks.data() + index * kFeatureDim);
    }
    return packed;
  }

} // namespace signlang::signlang_det
