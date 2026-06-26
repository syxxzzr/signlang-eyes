#include "feature_extractor.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

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
      if (detection.confidence >= min_confidence_) {
        hands.push_back(&detection);
      }
    }

    // Sort hands left-to-right by x-coordinate
    std::sort(hands.begin(), hands.end(),
              [this](const auto* a, const auto* b) { return compute_hand_center(*a) < compute_hand_center(*b); });

    // Take at most 2 hands
    if (hands.size() > kMaxHandCount) {
      hands.resize(kMaxHandCount);
    }

    return hands;
  }

  auto FeatureExtractor::compute_hand_center(const handpose_det::HandPoseDetection& hand) const -> float {
    auto sum_x = 0.0F;
    for (const auto& kp : hand.keypoints) {
      sum_x += kp.x;
    }
    return sum_x / static_cast<float>(hand.keypoints.size());
  }

  auto FeatureExtractor::compute_center_distance(const handpose_det::HandPoseDetection& current,
                                                 const HandSlot& previous) const -> float {
    auto current_x = 0.0F;
    auto current_y = 0.0F;
    auto prev_x = 0.0F;
    auto prev_y = 0.0F;

    for (std::size_t i = 0; i < current.keypoints.size(); ++i) {
      current_x += current.keypoints[i].x;
      current_y += current.keypoints[i].y;
      prev_x += previous.keypoints[i].x;
      prev_y += previous.keypoints[i].y;
    }

    const auto n = static_cast<float>(current.keypoints.size());
    current_x /= n;
    current_y /= n;
    prev_x /= n;
    prev_y /= n;

    const auto dx = current_x - prev_x;
    const auto dy = current_y - prev_y;
    return std::sqrt(dx * dx + dy * dy);
  }

  auto FeatureExtractor::assign_hands_to_slots(const std::vector<const handpose_det::HandPoseDetection*>& hands)
      -> std::array<const handpose_det::HandPoseDetection*, kMaxHandCount> {
    auto assigned = std::array<const handpose_det::HandPoseDetection*, kMaxHandCount>{nullptr, nullptr};

    if (hands.size() == kMaxHandCount) {
      assigned[0] = hands[0];
      assigned[1] = hands[1];
      return assigned;
    }

    auto available_slots = std::vector<std::uint32_t>{};
    for (std::uint32_t i = 0; i < kMaxHandCount; ++i) {
      available_slots.push_back(i);
    }

    for (const auto* hand : hands) {
      auto previous_slots = std::vector<std::uint32_t>{};
      for (const auto slot : available_slots) {
        if (prev_hands_[slot].occupied) {
          previous_slots.push_back(slot);
        }
      }

      std::uint32_t best_slot = 0;
      if (!previous_slots.empty()) {
        auto min_distance = std::numeric_limits<float>::max();
        for (const auto slot : previous_slots) {
          const auto distance = compute_center_distance(*hand, prev_hands_[slot]);
          if (distance < min_distance) {
            min_distance = distance;
            best_slot = slot;
          }
        }
      } else {
        best_slot = available_slots.front();
      }

      assigned[best_slot] = hand;
      available_slots.erase(std::remove(available_slots.begin(), available_slots.end(), best_slot),
                            available_slots.end());
    }

    return assigned;
  }

  auto FeatureExtractor::compute_bounding_box_scale(
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

  auto FeatureExtractor::extract_single_hand(const handpose_det::HandPoseDetection& hand, std::uint32_t hand_index,
                                             bool sequence_continuous) -> HandFeatures {
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

  auto FeatureExtractor::extract(const handpose_det::HandPoseFrameMetadata& metadata,
                                 const handpose_det::HandPoseDetection* detections, std::uint32_t detection_count)
      -> std::optional<FeatureVector> {
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

} // namespace signlang::signlang_det
