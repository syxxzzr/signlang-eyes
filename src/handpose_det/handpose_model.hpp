#ifndef SIGNLANG_EYES_HANDPOSE_DET_HANDPOSE_MODEL_HPP
#define SIGNLANG_EYES_HANDPOSE_DET_HANDPOSE_MODEL_HPP

#include "handpose_frame.hpp"
#include "program_options.hpp"
#include "rknn_api.h"
#include "video_frontend/video_frame.hpp"

#include "iox2/bb/slice.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace signlang::handpose_det {

  struct InferenceResult {
    std::uint32_t detection_count;
    std::uint32_t image_width;
    std::uint32_t image_height;
    std::uint32_t model_width;
    std::uint32_t model_height;
  };

  class HandPoseModel {
  public:
    HandPoseModel(std::string palm_detector_model_path, std::string landmark_model_path, const ProgramOptions& options,
                  std::uint32_t hand_slots);
    ~HandPoseModel();

    HandPoseModel(const HandPoseModel&) = delete;
    auto operator=(const HandPoseModel&) -> HandPoseModel& = delete;
    HandPoseModel(HandPoseModel&&) = delete;
    auto operator=(HandPoseModel&&) -> HandPoseModel& = delete;

    auto run(const signlang::video_frontend::VideoFrameMetadata& metadata, const std::uint8_t* payload,
             std::uint64_t payload_size, iox2::bb::MutableSlice<HandPoseDetection> detections) -> InferenceResult;

  private:
    struct RknnModel;
    struct Anchor;
    struct PalmCandidate {
      HandPoseDetection detection;
      std::array<float, 14> palm_keypoints;
      float handedness_score;
    };
    struct CropTransform {
      float left;
      float top;
      float size;
    };
    struct TrackedHand {
      HandPoseBox roi;
      float palm_confidence;
      float presence_confidence;
      std::uint64_t last_seen_frame;
      bool is_left_hand;
      float handedness_score;
      std::array<HandPoseKeypoint, kHandPoseKeypointCount> smoothed_keypoints;
    };
    struct OneEuroFilter {
      float x;
      float dx;
      float min_cutoff;
      float beta;
      float d_cutoff;
      std::uint64_t last_time;
      bool initialized;
    };

    void initialize_models(const ProgramOptions& options);
    void validate_models() const;
    void print_tensor_details() const;
    void build_palm_anchors();

    void run_palm_detector(const signlang::video_frontend::VideoFrameMetadata& metadata, const std::uint8_t* payload,
                           std::uint64_t payload_size);

    void run_landmark_detector(const signlang::video_frontend::VideoFrameMetadata& metadata,
                               const std::uint8_t* payload, std::uint64_t payload_size, PalmCandidate& candidate);

    auto extract_landmarks(const signlang::video_frontend::VideoFrameMetadata& metadata, const std::uint8_t* payload,
                           std::uint64_t payload_size, const CropTransform& transform,
                           std::array<HandPoseKeypoint, kHandPoseKeypointCount>& out, float& out_presence,
                           bool& out_is_left, float& out_handedness_score) const -> bool;

    void try_tracking_from_previous_frame(const signlang::video_frontend::VideoFrameMetadata& metadata,
                                          const std::uint8_t* payload, std::uint64_t payload_size);

    void update_tracked_hands(const std::vector<PalmCandidate>& detected_hands);
    void apply_nms();
    auto should_run_full_frame_detection(std::uint32_t tracked_count) const -> bool;

    [[nodiscard]] static auto compute_iou(const HandPoseBox& box1, const HandPoseBox& box2) -> float;

    auto crop_transform_from_box(const HandPoseBox& box, std::uint32_t image_width, std::uint32_t image_height) const
        -> CropTransform;

    auto crop_transform_from_palm_keypoints(const std::array<float, 14>& palm_keypoints, std::uint32_t image_width,
                                            std::uint32_t image_height) const -> CropTransform;

    [[nodiscard]] static auto apply_one_euro_filter(OneEuroFilter& filter, float value, std::uint64_t timestamp_ns)
        -> float;

    void smooth_keypoints_hand(std::size_t hand_index, std::uint64_t timestamp_ns);
    void ensure_source_dma_buffer(std::uint64_t required_size) const;
    auto source_dma_data() const -> std::uint8_t*;
    auto source_dma_fd() const -> int;
    void sync_source_dma_buffer(std::uint64_t flags) const;
    void release_source_dma_buffer() const;

    std::string palm_detector_model_path_;
    std::string landmark_model_path_;
    std::unique_ptr<RknnModel> palm_detector_;
    std::unique_ptr<RknnModel> landmark_model_;
    std::vector<Anchor> anchors_;
    std::vector<PalmCandidate> candidates_;
    std::vector<PalmCandidate> selected_;
    std::vector<TrackedHand> tracked_hands_;
    std::vector<std::vector<OneEuroFilter>> keypoint_filters_;
    std::uint64_t current_frame_number_;
    float confidence_threshold_;
    float presence_threshold_;
    float tracking_threshold_;
    float nms_iou_threshold_;
    float tracking_iou_match_threshold_;
    float crop_expansion_;
    float base_roi_expansion_;
    float small_hand_expansion_;
    float large_hand_expansion_;
    float small_hand_threshold_;
    float large_hand_threshold_;
    float boundary_margin_;
    float boundary_min_factor_;
    float euro_min_cutoff_;
    float euro_beta_;
    float euro_d_cutoff_;
    float handedness_threshold_;
    bool swap_handedness_;
    std::uint32_t max_tracking_gap_;
    std::uint32_t max_stale_frames_;
    std::uint32_t full_frame_interval_;
    std::uint32_t previous_tracked_count_;
    const std::uint32_t hand_slots_;
    std::uint32_t model_width_;
    std::uint32_t model_height_;
    mutable int source_dma_fd_;
    mutable std::uint8_t* source_dma_data_;
    mutable std::uint64_t source_dma_capacity_;
  };

} // namespace signlang::handpose_det

#endif // SIGNLANG_EYES_HANDPOSE_DET_HANDPOSE_MODEL_HPP
