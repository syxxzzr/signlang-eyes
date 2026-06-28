#ifndef SIGNLANG_EYES_SIGNLANG_MANAGER_GESTURE_FEATURE_EXTRACTOR_HPP
#define SIGNLANG_EYES_SIGNLANG_MANAGER_GESTURE_FEATURE_EXTRACTOR_HPP

#include "gesture_types.hpp"
#include "handpose_det/handpose_frame.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace signlang::signlang_manager {

  struct HandSlot {
    std::array<handpose_det::HandPoseKeypoint, handpose_det::kHandPoseKeypointCount> keypoints;
    bool occupied;
  };

  class GestureFeatureExtractor {
  public:
    explicit GestureFeatureExtractor(float min_confidence);

    auto extract(const handpose_det::HandPoseFrameMetadata& metadata, const handpose_det::HandPoseDetection* detections,
                 std::uint32_t detection_count) -> std::optional<FeatureVector>;

    void reset();

  private:
    auto prepare_hands(const handpose_det::HandPoseDetection* detections, std::uint32_t count) const
        -> std::vector<const handpose_det::HandPoseDetection*>;
    auto assign_hands_to_slots(const std::vector<const handpose_det::HandPoseDetection*>& hands)
        -> std::array<const handpose_det::HandPoseDetection*, kMaxHandCount>;
    auto extract_single_hand(const handpose_det::HandPoseDetection& hand, std::uint32_t hand_index,
                             bool sequence_continuous) -> HandFeatures;
    auto compute_bounding_box_scale(
        const std::array<handpose_det::HandPoseKeypoint, handpose_det::kHandPoseKeypointCount>& keypoints) const
        -> float;

    float min_confidence_;
    std::array<HandSlot, kMaxHandCount> prev_hands_;
    std::uint64_t prev_sequence_number_{0};
  };

} // namespace signlang::signlang_manager

#endif // SIGNLANG_EYES_SIGNLANG_MANAGER_GESTURE_FEATURE_EXTRACTOR_HPP
