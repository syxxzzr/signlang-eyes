#include "feature_extractor.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace signlang::signlang_det {
  namespace {

    constexpr auto kScaleEpsilon = float{1e-6F};

  } // namespace

  FeatureExtractor::FeatureExtractor(float min_confidence) : min_confidence_(min_confidence) {
    for (auto& slot : prev_hands_) {
      slot.occupied = false;
    }
  }

  void FeatureExtractor::reset() {
    for (auto& slot : prev_hands_) {
      slot.occupied = false;
    }
    prev_sequence_number_ = 0;
  }

  auto FeatureExtractor::prepare_hands(const handpose_det::HandPoseDetection* detections, std::uint32_t count) const
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

  auto FeatureExtractor::assign_hands_to_slots(const std::vector<const handpose_det::HandPoseDetection*>& hands)
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

  auto FeatureExtractor::compute_hand_scale(
      const std::array<handpose_det::HandPoseKeypoint, handpose_det::kHandPoseKeypointCount>& keypoints) -> float {
    const auto& wrist = keypoints[0];
    auto max_distance = 0.0F;

    for (const auto& kp : keypoints) {
      const auto dx = kp.x - wrist.x;
      const auto dy = kp.y - wrist.y;
      const auto dz = kp.z - wrist.z;
      max_distance = std::max(max_distance, std::sqrt(dx * dx + dy * dy + dz * dz));
    }

    return max_distance + kScaleEpsilon;
  }

  auto FeatureExtractor::normalize_keypoints(
      const handpose_det::HandPoseFrameMetadata& metadata,
      const std::array<handpose_det::HandPoseKeypoint, handpose_det::kHandPoseKeypointCount>& keypoints)
      -> std::array<handpose_det::HandPoseKeypoint, handpose_det::kHandPoseKeypointCount> {
    const auto width = std::max(1.0F, static_cast<float>(metadata.image_width));
    const auto height = std::max(1.0F, static_cast<float>(metadata.image_height));

    auto normalized = keypoints;
    for (auto& kp : normalized) {
      kp.x /= width;
      kp.y /= height;
      kp.z /= width;
    }
    return normalized;
  }

  auto FeatureExtractor::extract_single_hand(const handpose_det::HandPoseFrameMetadata& metadata,
                                             const handpose_det::HandPoseDetection& hand, std::uint32_t hand_index,
                                             bool sequence_continuous) -> HandFeatures {
    const auto keypoints = normalize_keypoints(metadata, hand.keypoints);
    const auto& wrist = keypoints[0];
    const auto scale = compute_hand_scale(keypoints);

    auto features = HandFeatures{};
    features.present = true;
    features.confidence = hand.confidence;
    features.motion_score = 0.0F;

    const auto has_prev = prev_hands_[hand_index].occupied && sequence_continuous;
    auto velocities = std::vector<float>{};
    velocities.reserve(keypoints.size());
    auto shape_motion = 0.0F;

    for (std::size_t i = 0; i < keypoints.size(); ++i) {
      const auto position = std::array<float, 3>{(keypoints[i].x - wrist.x) / scale,
                                                 (keypoints[i].y - wrist.y) / scale,
                                                 (keypoints[i].z - wrist.z) / scale};
      features.features[i].normalized_x = position[0];
      features.features[i].normalized_y = position[1];
      features.features[i].normalized_z = position[2];

      if (has_prev) {
        const auto& previous = prev_hands_[hand_index].positions[i];
        const auto dx = position[0] - previous[0];
        const auto dy = position[1] - previous[1];
        const auto dz = position[2] - previous[2];
        features.features[i].velocity_magnitude = std::sqrt(dx * dx + dy * dy + dz * dz);
      } else {
        features.features[i].velocity_magnitude = 0.0F;
      }
      velocities.push_back(features.features[i].velocity_magnitude);
      if (i != 0) {
        shape_motion += features.features[i].velocity_magnitude;
      }
      prev_hands_[hand_index].positions[i] = position;
    }
    std::sort(velocities.begin(), velocities.end());
    const auto p75 = velocities[static_cast<std::size_t>(0.75F * static_cast<float>(velocities.size() - 1U))];
    const auto wrist_position = std::array<float, 3>{wrist.x, wrist.y, wrist.z};
    auto center_motion = 0.0F;
    if (has_prev) {
      const auto dx = wrist_position[0] - prev_hands_[hand_index].wrist[0];
      const auto dy = wrist_position[1] - prev_hands_[hand_index].wrist[1];
      const auto dz = wrist_position[2] - prev_hands_[hand_index].wrist[2];
      center_motion = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    shape_motion /= static_cast<float>(keypoints.size() - 1U);
    features.motion_score = 0.5F * p75 + 0.25F * center_motion + 0.25F * shape_motion;
    prev_hands_[hand_index].wrist = wrist_position;
    prev_hands_[hand_index].occupied = true;

    return features;
  }

  auto FeatureExtractor::extract(const handpose_det::HandPoseFrameMetadata& metadata,
                                 const handpose_det::HandPoseDetection* detections, std::uint32_t detection_count)
      -> FeatureVector {
    const auto hands = prepare_hands(detections, detection_count);
    const auto assigned_hands = assign_hands_to_slots(hands);

    const auto sequence_continuous = prev_sequence_number_ == 0 || metadata.source_sequence_number == prev_sequence_number_ + 1;

    auto feature = FeatureVector{};
    feature.source_sequence_number = metadata.source_sequence_number;
    feature.timestamp_ns = metadata.timestamp_ns;
    feature.sequence_continuous = sequence_continuous;

    auto confidence_sum = 0.0F;
    auto confidence_count = 0U;
    auto motion_sum = 0.0F;
    auto motion_count = 0U;

    for (std::uint32_t i = 0; i < kMaxHandCount; ++i) {
      if (assigned_hands[i] != nullptr) {
        feature.hands[i] = extract_single_hand(metadata, *assigned_hands[i], i, sequence_continuous);
      } else {
        feature.hands[i].present = false;
        feature.hands[i].confidence = 0.0F;
        feature.hands[i].motion_score = 0.0F;
        for (auto& kp_feat : feature.hands[i].features) {
          kp_feat.normalized_x = 0.0F;
          kp_feat.normalized_y = 0.0F;
          kp_feat.normalized_z = 0.0F;
          kp_feat.velocity_magnitude = 0.0F;
        }
        prev_hands_[i].occupied = false;
      }

      if (feature.hands[i].present) {
        confidence_sum += feature.hands[i].confidence;
        ++confidence_count;
        motion_sum += feature.hands[i].motion_score;
        ++motion_count;
      }
    }

    feature.mean_confidence = confidence_count == 0 ? 0.0F : confidence_sum / static_cast<float>(confidence_count);
    feature.motion_score = motion_count == 0 ? 0.0F : motion_sum / static_cast<float>(motion_count);

    prev_sequence_number_ = metadata.source_sequence_number;

    return feature;
  }

} // namespace signlang::signlang_det
