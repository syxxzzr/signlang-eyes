#ifndef SIGNLANG_EYES_SIGNLANG_DET_FEATURE_EXTRACTOR_HPP
#define SIGNLANG_EYES_SIGNLANG_DET_FEATURE_EXTRACTOR_HPP

#include "handpose_det/handpose_frame.hpp"
#include "signlang_result.hpp"

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

    [[nodiscard]] auto extract(const handpose_det::HandPoseFrameMetadata& metadata,
                               const handpose_det::HandPoseDetection* detections, std::uint32_t detection_count)
        -> std::optional<FeatureVector>;

    void reset();

  private:
    [[nodiscard]] auto prepare_hands(const handpose_det::HandPoseDetection* detections, std::uint32_t count) const
        -> std::vector<const handpose_det::HandPoseDetection*>;

    [[nodiscard]] static auto assign_hands_to_slots(const std::vector<const handpose_det::HandPoseDetection*>& hands)
        -> std::array<const handpose_det::HandPoseDetection*, kMaxHandCount>;

    auto extract_single_hand(const handpose_det::HandPoseFrameMetadata& metadata,
                             const handpose_det::HandPoseDetection& hand, std::uint32_t hand_index,
                             bool sequence_continuous) -> HandFeatures;

    [[nodiscard]] static auto compute_bounding_box_scale(
        const std::array<handpose_det::HandPoseKeypoint, handpose_det::kHandPoseKeypointCount>& keypoints) -> float;

    [[nodiscard]] static auto normalize_keypoints(
        const handpose_det::HandPoseFrameMetadata& metadata,
        const std::array<handpose_det::HandPoseKeypoint, handpose_det::kHandPoseKeypointCount>& keypoints)
        -> std::array<handpose_det::HandPoseKeypoint, handpose_det::kHandPoseKeypointCount>;

    float min_confidence_;
    std::array<HandSlot, kMaxHandCount> prev_hands_{};
    std::uint64_t prev_sequence_number_{0};
  };

} // namespace signlang::signlang_det

#endif // SIGNLANG_EYES_SIGNLANG_DET_FEATURE_EXTRACTOR_HPP
