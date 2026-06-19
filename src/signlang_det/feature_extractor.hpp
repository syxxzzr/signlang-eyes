#ifndef SIGNLANG_EYES_SIGNLANG_DET_FEATURE_EXTRACTOR_HPP
#define SIGNLANG_EYES_SIGNLANG_DET_FEATURE_EXTRACTOR_HPP

#include "signlang_result.hpp"
#include "handpose_det/handpose_frame.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace signlang::signlang_det {

  struct HandSlot {
    std::array<handpose_det::HandPoseKeypoint, handpose_det::kHandPoseKeypointCount> keypoints;
    bool occupied;
  };

  class FeatureExtractor {
  public:
    explicit FeatureExtractor(float min_confidence);

    auto extract(const handpose_det::HandPoseFrameMetadata& metadata,
                 const handpose_det::HandPoseDetection* detections,
                 std::uint32_t detection_count)
      -> std::optional<FeatureVector>;

    void reset();

  private:
    auto prepare_hands(const handpose_det::HandPoseDetection* detections,
                       std::uint32_t count) const
      -> std::vector<const handpose_det::HandPoseDetection*>;

    auto assign_hands_to_slots(const std::vector<const handpose_det::HandPoseDetection*>& hands)
      -> std::array<const handpose_det::HandPoseDetection*, kMaxHandCount>;

    auto compute_hand_center(const handpose_det::HandPoseDetection& hand) const -> float;

    auto compute_center_distance(const handpose_det::HandPoseDetection& current,
                                 const HandSlot& previous) const -> float;

    auto extract_single_hand(const handpose_det::HandPoseDetection& hand,
                            std::uint32_t hand_index,
                            bool sequence_continuous)
      -> HandFeatures;

    auto compute_bounding_box_scale(
      const std::array<handpose_det::HandPoseKeypoint, handpose_det::kHandPoseKeypointCount>& keypoints) const
      -> float;

    float min_confidence_;
    std::array<HandSlot, kMaxHandCount> prev_hands_;
    std::uint64_t prev_sequence_number_{0};
  };

} // namespace signlang::signlang_det

#endif // SIGNLANG_EYES_SIGNLANG_DET_FEATURE_EXTRACTOR_HPP
