#ifndef SIGNLANG_EYES_SIGNLANG_DET_SIGNLANG_MODEL_HPP
#define SIGNLANG_EYES_SIGNLANG_DET_SIGNLANG_MODEL_HPP

#include "signlang_result.hpp"

#include "rknn_api.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace signlang::signlang_det {

  struct GestureLabelMap {
    std::vector<std::string> labels;
  };

  struct GesturePrototype {
    std::vector<std::vector<float>> encoded_frames;
  };

  auto load_gesture_labels(const std::string& path) -> GestureLabelMap;
  auto load_gesture_prototypes(const std::string& path) 
    -> std::unordered_map<std::uint32_t, GesturePrototype>;

  class SignlangModel {
  public:
    struct InferenceResult {
      std::uint32_t gesture_id;
      float inference_time_ms;
      float confidence;
      float second_confidence;  // For margin check
    };

    SignlangModel(const std::string& model_path,
                  const std::string& label_map_path,
                  const std::string& prototypes_path,
                  rknn_core_mask npu_core,
                  float motion_weight = 0.0F,
                  float dtw_window_ratio = 0.5F);

    SignlangModel(const SignlangModel&) = delete;
    auto operator=(const SignlangModel&) -> SignlangModel& = delete;
    SignlangModel(SignlangModel&&) = delete;
    auto operator=(SignlangModel&&) -> SignlangModel& = delete;

    ~SignlangModel();

    auto infer(const std::vector<FeatureVector>& sequence) -> InferenceResult;
    auto get_gesture_name(std::uint32_t gesture_id) const -> const char*;

  private:
    void load_model(const std::string& model_path, rknn_core_mask npu_core);
    void query_io_info();
    void flatten_features(const std::vector<FeatureVector>& sequence);
    void prepare_motion_weighting();
    
    auto encode_sequence() -> std::vector<std::vector<float>>;
    auto dtw_match(const std::vector<std::vector<float>>& query_frames) -> InferenceResult;
    auto compute_dtw_distance(const std::vector<std::vector<float>>& query,
                              const std::vector<std::vector<float>>& sample) const -> float;
    auto compute_frame_distance(const std::vector<float>& query_frame,
                                const std::vector<float>& sample_frame) const -> float;
    auto compute_dtw_window(std::uint32_t query_length,
                            std::uint32_t sample_length) const -> std::uint32_t;

    rknn_context ctx_{0};
    rknn_input_output_num io_num_{};
    rknn_tensor_attr input_attr_{};
    rknn_tensor_attr output_attr_{};
    std::uint32_t expected_sequence_length_{0};
    std::uint32_t frame_embedding_dim_{0};
    std::vector<float> input_buffer_;
    std::vector<std::uint32_t> motion_indices_;
    float motion_weight_;
    float dtw_window_ratio_;

    GestureLabelMap labels_;
    std::unordered_map<std::uint32_t, GesturePrototype> prototypes_;
  };

} // namespace signlang::signlang_det

#endif // SIGNLANG_EYES_SIGNLANG_DET_SIGNLANG_MODEL_HPP
