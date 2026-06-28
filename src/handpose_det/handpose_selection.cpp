#include "handpose_selection.hpp"

#include <optional>

namespace signlang::handpose_det {
  namespace {

    struct BestCandidate {
      std::uint32_t index;
      float confidence;
      float score;
    };

    void consider_candidate(std::optional<BestCandidate>& best, std::uint32_t index, float confidence, float score) {
      if (!best.has_value() || confidence > best->confidence) {
        best = BestCandidate{.index = index, .confidence = confidence, .score = score};
      }
    }

  } // namespace

  auto select_handedness_detections(std::span<const HandednessCandidate> candidates, float handedness_threshold,
                                    bool swap_handedness, std::size_t max_output_count)
      -> std::vector<HandednessSelection> {
    auto best_left = std::optional<BestCandidate>{};
    auto best_right = std::optional<BestCandidate>{};

    for (const auto& candidate : candidates) {
      auto left_score = candidate.handedness_score;
      auto right_score = 1.0F - candidate.handedness_score;
      if (swap_handedness) {
        const auto raw_left_score = left_score;
        left_score = right_score;
        right_score = raw_left_score;
      }

      if (left_score > handedness_threshold) {
        consider_candidate(best_left, candidate.index, candidate.confidence, left_score);
      }
      if (right_score > handedness_threshold) {
        consider_candidate(best_right, candidate.index, candidate.confidence, right_score);
      }
    }

    auto selected = std::vector<HandednessSelection>{};
    selected.reserve(max_output_count);

    if (best_left.has_value() && best_right.has_value() && best_left->index == best_right->index) {
      if (best_left->score >= best_right->score) {
        best_right.reset();
      } else {
        best_left.reset();
      }
    }

    if (best_left.has_value() && selected.size() < max_output_count) {
      selected.push_back(HandednessSelection{.candidate_index = best_left->index, .is_left_hand = true});
    }
    if (best_right.has_value() && selected.size() < max_output_count) {
      selected.push_back(HandednessSelection{.candidate_index = best_right->index, .is_left_hand = false});
    }

    return selected;
  }

} // namespace signlang::handpose_det
