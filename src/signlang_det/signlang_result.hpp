#ifndef SIGNLANG_EYES_SIGNLANG_DET_SIGNLANG_RESULT_HPP
#define SIGNLANG_EYES_SIGNLANG_DET_SIGNLANG_RESULT_HPP

#include <array>
#include <cstdint>
#include <type_traits>

namespace signlang::signlang_det {

  constexpr auto kKeypointCount = std::uint32_t{21};
  constexpr auto kMaxHandCount = std::uint32_t{2};
  constexpr auto kFeatureDimPerHand = std::uint32_t{63};
  constexpr auto kFeatureDim = std::uint32_t{126}; // 2 hands * 63
  constexpr auto kMaxGestureNameLength = std::uint32_t{64};

  struct KeypointFeature {
    float normalized_x;
    float normalized_y;
    float velocity_magnitude;
  };

  struct HandFeatures {
    std::array<KeypointFeature, kKeypointCount> features;
    bool present; // Whether this hand slot has valid data
  };

  struct FeatureVector {
    std::array<HandFeatures, kMaxHandCount> hands; // hands[0]=left, hands[1]=right
    std::uint64_t source_sequence_number;
    std::uint64_t timestamp_ns;
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
    std::uint32_t gesture_id;
    std::array<char, kMaxGestureNameLength> gesture_name;
  };

  static_assert(std::is_trivially_copyable_v<KeypointFeature>);
  static_assert(std::is_trivially_copyable_v<HandFeatures>);
  static_assert(std::is_trivially_copyable_v<FeatureVector>);
  static_assert(std::is_trivially_copyable_v<SignlangResult>);

  auto steady_timestamp_ns() -> std::uint64_t;
  void copy_string(const char* source, std::array<char, kMaxGestureNameLength>& dest);

} // namespace signlang::signlang_det

#endif // SIGNLANG_EYES_SIGNLANG_DET_SIGNLANG_RESULT_HPP
