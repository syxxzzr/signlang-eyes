#ifndef SIGNLANG_EYES_EDGEAI_HANDPOSE_DET_HANDPOSE_MODEL_HPP
#define SIGNLANG_EYES_EDGEAI_HANDPOSE_DET_HANDPOSE_MODEL_HPP

#include "handpose_frame.hpp"
#include "handpose_preprocessor.hpp"
#include "program_options.hpp"
#include "rknn/include/rknn_api.h"
#include "rknn_runtime.hpp"
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
    HandPoseModel(std::string model_path, std::string runtime_library_path, const ProgramOptions& options);
    ~HandPoseModel();

    HandPoseModel(const HandPoseModel&) = delete;
    auto operator=(const HandPoseModel&) -> HandPoseModel& = delete;
    HandPoseModel(HandPoseModel&&) = delete;
    auto operator=(HandPoseModel&&) -> HandPoseModel& = delete;

    auto run(const signlang::video_frontend::VideoFrameMetadata& metadata, const std::uint8_t* payload,
             std::uint64_t payload_size, iox2::bb::MutableSlice<HandPoseDetection> detections) -> InferenceResult;

  private:
    struct Candidate {
      HandPoseDetection detection;
      std::uint32_t index;
      bool suppressed;
    };

    void load_model(const std::string& model_path, const ProgramOptions& options);
    void configure_io(const ProgramOptions& options);
    void print_tensor_details() const;
    auto input_stride_width_pixels() const -> std::uint32_t;
    auto output_stride_candidate_count() const -> std::uint32_t;
    auto output_value(std::uint32_t channel, std::uint32_t candidate_index) const -> float;
    auto parse_output(const LetterboxInfo& letterbox, const signlang::video_frontend::VideoFrameMetadata& metadata,
                      iox2::bb::MutableSlice<HandPoseDetection> detections) -> std::uint32_t;

    std::string model_path_;
    RknnRuntime runtime_;
    rknn_context context_;
    rknn_input_output_num io_num_;
    std::vector<rknn_tensor_attr> input_attrs_;
    std::vector<rknn_tensor_attr> output_attrs_;
    rknn_tensor_mem* input_mem_;
    rknn_tensor_mem* output_mem_;
    HandPosePreprocessor preprocessor_;
    std::vector<Candidate> candidates_;
    std::vector<std::uint32_t> order_;
    float confidence_threshold_;
    float nms_threshold_;
    std::uint32_t keypoint_count_;
    std::uint32_t max_detections_;
    std::uint32_t model_width_;
    std::uint32_t model_height_;
    std::uint32_t output_channel_count_;
    std::uint32_t output_candidate_count_;
    std::uint32_t output_stride_candidate_count_;
  };

} // namespace signlang::handpose_det

#endif // SIGNLANG_EYES_EDGEAI_HANDPOSE_DET_HANDPOSE_MODEL_HPP
