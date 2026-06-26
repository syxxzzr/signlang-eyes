#ifndef SIGNLANG_EYES_SIGNLANG_MANAGER_GESTURE_TYPES_HPP
#define SIGNLANG_EYES_SIGNLANG_MANAGER_GESTURE_TYPES_HPP

#include "handpose_det/handpose_frame.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace signlang::signlang_manager {

  constexpr auto kMaxHandCount = std::uint32_t{2};
  constexpr auto kFeatureChannelsPerKeypoint = std::uint32_t{4};
  constexpr auto kFeatureDimPerHand = signlang::handpose_det::kHandPoseKeypointCount * kFeatureChannelsPerKeypoint;
  constexpr auto kFeatureDim = kMaxHandCount * kFeatureDimPerHand;

  struct KeypointFeature {
    float normalized_x;
    float normalized_y;
    float normalized_z;
    float velocity_magnitude;
  };

  struct HandFeatures {
    std::array<KeypointFeature, signlang::handpose_det::kHandPoseKeypointCount> features;
    bool present;
  };

  struct FeatureVector {
    std::array<HandFeatures, kMaxHandCount> hands;
    std::uint64_t source_sequence_number;
    std::uint64_t timestamp_ns;
  };

  using EncodedSequence = std::vector<std::vector<float>>;

  struct GestureInfo {
    std::uint32_t id;
    std::string name;
    bool enabled;
    std::uint32_t sample_count;
  };

} // namespace signlang::signlang_manager

#endif // SIGNLANG_EYES_SIGNLANG_MANAGER_GESTURE_TYPES_HPP
