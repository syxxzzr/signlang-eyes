#ifndef SIGNLANG_EYES_HANDPOSE_DET_PROGRAM_OPTIONS_HPP
#define SIGNLANG_EYES_HANDPOSE_DET_PROGRAM_OPTIONS_HPP

#include "common/logging.hpp"
#include "rknn_api.h"

#include <string>
#include <variant>

namespace signlang::handpose_det {

  constexpr auto kDefaultPalmDetectorModelPath = "models/mediapipe/hand_detector.rknn";
  constexpr auto kDefaultLandmarkModelPath = "models/mediapipe/hand_landmarks_detector.rknn";
  constexpr auto kDefaultConfidenceThreshold = 0.5F;
  constexpr auto kDefaultPresenceThreshold = 0.5F;
  constexpr auto kDefaultTrackingThreshold = 0.5F;
  constexpr auto kDefaultNmsIouThreshold = 0.3F;
  constexpr auto kDefaultTrackingIoUMatchThreshold = 0.3F;
  constexpr auto kDefaultCropExpansion = 2.0F;
  constexpr auto kDefaultBaseRoiExpansion = 2.6F;
  constexpr auto kDefaultSmallHandExpansion = 3.0F;
  constexpr auto kDefaultLargeHandExpansion = 2.3F;
  constexpr auto kDefaultSmallHandThreshold = 60.0F;
  constexpr auto kDefaultLargeHandThreshold = 120.0F;
  constexpr auto kDefaultBoundaryMargin = 50.0F;
  constexpr auto kDefaultBoundaryMinFactor = 0.7F;
  constexpr auto kDefaultEuroMinCutoff = 1.0F;
  constexpr auto kDefaultEuroBeta = 0.007F;
  constexpr auto kDefaultEuroDCutoff = 1.0F;
  constexpr auto kDefaultHandednessThreshold = 0.5F;
  constexpr auto kDefaultMaxTrackingGap = std::uint32_t{2};
  constexpr auto kDefaultMaxStaleFrames = std::uint32_t{5};
  constexpr auto kDefaultFullFrameInterval = std::uint32_t{10};
  constexpr auto kDefaultSubscriberBufferSize = std::uint64_t{2};
  constexpr auto kDefaultSingleHand = false;
  constexpr auto kDefaultSwapHandedness = false;

  struct ProgramOptions {
    std::string input_service_name;
    std::string output_service_name;
    std::string palm_detector_model_path;
    std::string landmark_model_path;
    float confidence_threshold;
    float presence_threshold;
    float tracking_threshold;
    float nms_iou_threshold;
    float tracking_iou_match_threshold;
    float crop_expansion;
    float base_roi_expansion;
    float small_hand_expansion;
    float large_hand_expansion;
    float small_hand_threshold;
    float large_hand_threshold;
    float boundary_margin;
    float boundary_min_factor;
    float euro_min_cutoff;
    float euro_beta;
    float euro_d_cutoff;
    float handedness_threshold;
    std::uint32_t max_tracking_gap;
    std::uint32_t max_stale_frames;
    std::uint32_t full_frame_interval;
    std::uint64_t subscriber_buffer_size;
    bool single_hand;
    bool swap_handedness;
    rknn_core_mask palm_detector_npu_core_mask;
    rknn_core_mask landmark_npu_core_mask;
    bool verbose;
    signlang::logging::Options logging;
  };

  struct ProgramUsage {
    std::string text;
  };

  using ProgramOptionsParseResult = std::variant<ProgramOptions, ProgramUsage>;

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult;

} // namespace signlang::handpose_det

#endif // SIGNLANG_EYES_HANDPOSE_DET_PROGRAM_OPTIONS_HPP
