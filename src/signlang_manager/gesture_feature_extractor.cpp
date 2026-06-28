#include "gesture_feature_extractor.hpp"

#include <algorithm>
#include <cmath>

namespace signlang::signlang_manager {
  namespace {

    constexpr auto kScaleEpsilon = float{1e-6F};

  } // namespace

  GestureFeatureExtractor::GestureFeatureExtractor(float min_confidence) : min_confidence_{min_confidence} { reset(); }

  void GestureFeatureExtractor::reset() {
    for (auto& slot : prev_hands_) {
      slot.occupied = false;
    }
    prev_sequence_number_ = 0;
  }

  auto GestureFeatureExtractor::prepare_hands(const handpose_det::HandPoseDetection* detections,
                                              std::uint32_t count) const
      -> std::vector<const handpose_det::HandPoseDetection*> {
    auto hands = std::vector<const handpose_det::HandPoseDetection*>{};
    for (std::uint32_t i = 0; i < count; ++i) {
      const auto& detection = detections[i];
      if (detection.present && detection.confidence >= min_confidence_) {
        hands.push_back(&detection);
      }
    }

    if (hands.size() > kMaxHandCount) {
      hands.resize(kMaxHandCount);
    }
    return hands;
  }

  auto GestureFeatureExtractor::assign_hands_to_slots(const std::vector<const handpose_det::HandPoseDetection*>& hands)
      -> std::array<const handpose_det::HandPoseDetection*, kMaxHandCount> {
    auto assigned = std::array<const handpose_det::HandPoseDetection*, kMaxHandCount>{nullptr, nullptr};

    for (const auto* hand : hands) {
      const auto slot = hand->is_left_hand ? 0U : 1U;
      if (assigned[slot] == nullptr || hand->confidence > assigned[slot]->confidence) {
        assigned[slot] = hand;
      }
    }

    return assigned;
  }

  auto GestureFeatureExtractor::compute_bounding_box_scale(
      const std::array<handpose_det::HandPoseKeypoint, handpose_det::kHandPoseKeypointCount>& keypoints) const
      -> float {
    const auto& wrist = keypoints[0];
    auto max_distance = 0.0F;
    for (const auto& kp : keypoints) {
      const auto dx = std::abs(kp.x - wrist.x);
      const auto dy = std::abs(kp.y - wrist.y);
      const auto dz = std::abs(kp.z - wrist.z);
      max_distance = std::max({max_distance, dx, dy, dz});
    }
    return max_distance + kScaleEpsilon;
  }

  auto GestureFeatureExtractor::extract_single_hand(const handpose_det::HandPoseDetection& hand,
                                                    std::uint32_t hand_index, bool sequence_continuous)
      -> HandFeatures {
    const auto& keypoints = hand.keypoints;
    const auto& wrist = keypoints[0];
    const auto scale = compute_bounding_box_scale(keypoints);

    auto features = HandFeatures{};
    features.present = true;
    const auto has_prev = prev_hands_[hand_index].occupied && sequence_continuous;

    for (std::size_t i = 0; i < keypoints.size(); ++i) {
      features.features[i].normalized_x = (keypoints[i].x - wrist.x) / scale;
      features.features[i].normalized_y = (keypoints[i].y - wrist.y) / scale;
      features.features[i].normalized_z = (keypoints[i].z - wrist.z) / scale;

      if (has_prev) {
        const auto& prev_kp = prev_hands_[hand_index].keypoints[i];
        const auto dx = (keypoints[i].x - prev_kp.x) / scale;
        const auto dy = (keypoints[i].y - prev_kp.y) / scale;
        const auto dz = (keypoints[i].z - prev_kp.z) / scale;
        features.features[i].velocity_magnitude = std::sqrt(dx * dx + dy * dy + dz * dz);
      } else {
        features.features[i].velocity_magnitude = 0.0F;
      }
    }

    prev_hands_[hand_index].keypoints = keypoints;
    prev_hands_[hand_index].occupied = true;
    return features;
  }

  auto GestureFeatureExtractor::extract(const handpose_det::HandPoseFrameMetadata& metadata,
                                        const handpose_det::HandPoseDetection* detections,
                                        std::uint32_t detection_count) -> std::optional<FeatureVector> {
    const auto hands = prepare_hands(detections, detection_count);
    if (hands.empty()) {
      return std::nullopt;
    }

    const auto assigned_hands = assign_hands_to_slots(hands);
    const auto sequence_continuous =
        (prev_sequence_number_ != 0) && (metadata.source_sequence_number == prev_sequence_number_ + 1);

    auto feature = FeatureVector{};
    feature.source_sequence_number = metadata.source_sequence_number;
    feature.timestamp_ns = metadata.timestamp_ns;

    for (std::uint32_t i = 0; i < kMaxHandCount; ++i) {
      if (assigned_hands[i] != nullptr) {
        feature.hands[i] = extract_single_hand(*assigned_hands[i], i, sequence_continuous);
      } else {
        feature.hands[i].present = false;
        for (auto& kp_feat : feature.hands[i].features) {
          kp_feat.normalized_x = 0.0F;
          kp_feat.normalized_y = 0.0F;
          kp_feat.normalized_z = 0.0F;
          kp_feat.velocity_magnitude = 0.0F;
        }
        if (!sequence_continuous) {
          prev_hands_[i].occupied = false;
        }
      }
    }

    prev_sequence_number_ = metadata.source_sequence_number;
    return feature;
  }

} // namespace signlang::signlang_manager
