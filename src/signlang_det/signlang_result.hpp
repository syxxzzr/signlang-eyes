#ifndef SIGNLANG_EYES_SIGNLANG_DET_SIGNLANG_RESULT_HPP
#define SIGNLANG_EYES_SIGNLANG_DET_SIGNLANG_RESULT_HPP

#include "handpose_det/handpose_frame.hpp"

#include <array>
#include <cstdint>
#include <type_traits>

namespace signlang::signlang_det {

  constexpr auto kMaxHandCount = std::uint32_t{2};
  constexpr auto kFeatureChannelsPerKeypoint = std::uint32_t{4};
  constexpr auto kFeatureDimPerHand = signlang::handpose_det::kHandPoseKeypointCount * kFeatureChannelsPerKeypoint;
  constexpr auto kFeatureDim = kMaxHandCount * kFeatureDimPerHand;
  constexpr auto kMaxGestureNameLength = std::uint32_t{64};
  constexpr auto kMaxGestureCandidates = std::uint32_t{5};

  struct KeypointFeature {
    float normalized_x;
    float normalized_y;
    float normalized_z;
    float velocity_magnitude;
  };

  struct HandFeatures {
    std::array<KeypointFeature, signlang::handpose_det::kHandPoseKeypointCount> features;
    bool present; // Whether this hand slot has valid data
  };

  struct FeatureVector {
    std::array<HandFeatures, kMaxHandCount> hands; // hands[0]=left, hands[1]=right
    std::uint64_t source_sequence_number;
    std::uint64_t timestamp_ns;
  };

  struct GestureCandidate {
    std::uint32_t gesture_id;
    float confidence;
    float distance;
    std::array<char, kMaxGestureNameLength> gesture_name;
  };

  struct SignlangResult {
    static constexpr const char* IOX2_TYPE_NAME = "signlang_signlang_det_result";

    std::uint64_t sequence_number;
    std::uint64_t timestamp_ns;
    std::uint64_t window_start_sequence;
    std::uint64_t window_end_sequence;
    std::uint32_t sequence_length;
    float overlap_ratio;
    float inference_time_ms;
    bool recognized;
    std::uint32_t gesture_id;
    float confidence;
    float second_confidence;
    float confidence_margin;
    float distance;
    std::array<char, kMaxGestureNameLength> gesture_name;
    std::uint32_t candidate_count;
    std::array<GestureCandidate, kMaxGestureCandidates> candidates;
  };

  static_assert(std::is_trivially_copyable_v<KeypointFeature>);
  static_assert(std::is_trivially_copyable_v<HandFeatures>);
  static_assert(std::is_trivially_copyable_v<FeatureVector>);
  static_assert(std::is_trivially_copyable_v<GestureCandidate>);
  static_assert(std::is_trivially_copyable_v<SignlangResult>);

  auto steady_timestamp_ns() -> std::uint64_t;
  void copy_string(const char* source, std::array<char, kMaxGestureNameLength>& dest);

} // namespace signlang::signlang_det

#endif // SIGNLANG_EYES_SIGNLANG_DET_SIGNLANG_RESULT_HPP
