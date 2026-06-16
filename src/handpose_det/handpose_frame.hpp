#ifndef SIGNLANG_EYES_EDGEAI_HANDPOSE_DET_HANDPOSE_FRAME_HPP
#define SIGNLANG_EYES_EDGEAI_HANDPOSE_DET_HANDPOSE_FRAME_HPP

#include <array>
#include <cstdint>
#include <type_traits>

namespace signlang::handpose_det {

  constexpr auto kHandPoseKeypointCount = std::uint32_t{21};
  constexpr auto kMaxHandPoseDetections = std::uint32_t{16};

  struct HandPoseKeypoint {
    float x;
    float y;
    float confidence;
  };

  struct HandPoseBox {
    float left;
    float top;
    float right;
    float bottom;
  };

  struct HandPoseDetection {
    HandPoseBox box;
    std::array<HandPoseKeypoint, kHandPoseKeypointCount> keypoints;
    float confidence;
    std::uint32_t class_id;
  };

  struct HandPoseFrameMetadata {
    std::uint64_t sequence_number;
    std::uint64_t timestamp_ns;
    std::uint64_t source_sequence_number;
    std::uint64_t source_timestamp_ns;
    std::uint32_t image_width;
    std::uint32_t image_height;
    std::uint32_t model_width;
    std::uint32_t model_height;
    std::uint32_t source_pixel_format;
    std::uint32_t detection_count;
    std::uint32_t keypoint_count;
    std::uint32_t payload_count;
  };

  static_assert(std::is_trivially_copyable_v<HandPoseKeypoint>);
  static_assert(std::is_trivially_copyable_v<HandPoseBox>);
  static_assert(std::is_trivially_copyable_v<HandPoseDetection>);
  static_assert(std::is_trivially_copyable_v<HandPoseFrameMetadata>);

} // namespace signlang::handpose_det

#endif // SIGNLANG_EYES_EDGEAI_HANDPOSE_DET_HANDPOSE_FRAME_HPP
