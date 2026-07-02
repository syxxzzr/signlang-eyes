#ifndef SIGNLANG_EYES_HANDPOSE_DET_HANDPOSE_SELECTION_HPP
#define SIGNLANG_EYES_HANDPOSE_DET_HANDPOSE_SELECTION_HPP

#include <cstdint>
#include <vector>

namespace signlang::handpose_det {

  struct HandednessCandidate {
    std::uint32_t index;
    float confidence;
    float handedness_score;
  };

  struct HandednessSelection {
    std::uint32_t candidate_index;
    bool is_left_hand;
  };

  auto select_handedness_detections(const std::vector<HandednessCandidate>& candidates, float handedness_threshold,
                                    bool swap_handedness, std::size_t max_output_count)
      -> std::vector<HandednessSelection>;

} // namespace signlang::handpose_det

#endif // SIGNLANG_EYES_HANDPOSE_DET_HANDPOSE_SELECTION_HPP
