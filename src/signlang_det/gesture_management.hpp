#ifndef SIGNLANG_EYES_SIGNLANG_DET_GESTURE_MANAGEMENT_HPP
#define SIGNLANG_EYES_SIGNLANG_DET_GESTURE_MANAGEMENT_HPP

#include "handpose_det/handpose_frame.hpp"
#include "signlang_result.hpp"

#include <array>
#include <cstdint>
#include <type_traits>

namespace signlang::signlang_det {

  constexpr auto kGestureManagementMessageLength = std::uint32_t{128};
  constexpr auto kGestureManagementMaxGestures = std::uint32_t{64};

  enum class GestureManagementCommand : std::uint32_t {
    GetStatus = 1,
    ListGestures = 2,
    AddGestureBegin = 3,
    AddGestureChunk = 4,
    AddGestureCommit = 5,
    AddGestureAbort = 6,
    DeleteGestureById = 7,
    DeleteGestureByName = 8,
  };

  enum class GestureManagementStatus : std::uint32_t {
    Ok = 0,
    BadRequest = 1,
    NotFound = 2,
    Failed = 3,
    UnsupportedCommand = 4,
  };

  struct GestureManagementGestureInfo {
    std::uint32_t id;
    std::uint32_t sample_count;
    bool enabled;
    bool calibrated;
    std::array<char, kMaxGestureNameLength> name;
  };

  struct GestureManagementRequest {
    static constexpr const char* IOX2_TYPE_NAME = "signlang_gesture_management_request";

    GestureManagementCommand command;
    std::uint32_t request_id;
    std::uint32_t transfer_id;
    std::uint32_t frame_count;
    std::uint32_t frame_index;
    std::uint32_t detection_count;
    std::uint32_t gesture_id;
    bool replace_existing;
    std::array<char, kMaxGestureNameLength> gesture_name;
    handpose_det::HandPoseFrameMetadata frame_metadata;
    std::array<handpose_det::HandPoseDetection, kMaxHandCount> detections;
  };

  struct GestureManagementResponse {
    static constexpr const char* IOX2_TYPE_NAME = "signlang_gesture_management_response";

    GestureManagementStatus status;
    std::uint32_t request_id;
    std::uint32_t gesture_id;
    std::uint32_t sequence_length;
    std::uint32_t embedding_dim;
    std::uint32_t loaded_gesture_count;
    std::uint32_t loaded_sample_count;
    std::uint32_t gesture_count;
    std::array<GestureManagementGestureInfo, kGestureManagementMaxGestures> gestures;
    std::array<char, kGestureManagementMessageLength> message;
  };

  static_assert(std::is_trivially_copyable_v<GestureManagementGestureInfo>);
  static_assert(std::is_trivially_copyable_v<GestureManagementRequest>);
  static_assert(std::is_trivially_copyable_v<GestureManagementResponse>);

} // namespace signlang::signlang_det

#endif // SIGNLANG_EYES_SIGNLANG_DET_GESTURE_MANAGEMENT_HPP
