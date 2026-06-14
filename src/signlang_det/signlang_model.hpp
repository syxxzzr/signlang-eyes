#ifndef SIGNLANG_EYES_EDGEAI_SIGNLANG_DET_SIGNLANG_MODEL_HPP
#define SIGNLANG_EYES_EDGEAI_SIGNLANG_DET_SIGNLANG_MODEL_HPP

#include "signlang_result.hpp"
#include "rknn_api.h"

#include <cstdint>
#include <string>
#include <vector>

namespace signlang::signlang_det {

struct GestureLabelMap {
  std::vector<std::string> labels;
};

auto load_gesture_labels(const std::string& path) -> GestureLabelMap;

class SignlangModel {
public:
  SignlangModel(const std::string& model_path,
                const std::string& label_map_path,
                rknn_core_mask npu_core);
  ~SignlangModel();

  SignlangModel(const SignlangModel&) = delete;
  auto operator=(const SignlangModel&) -> SignlangModel& = delete;
  SignlangModel(SignlangModel&&) = delete;
  auto operator=(SignlangModel&&) -> SignlangModel& = delete;

  struct InferenceResult {
    std::uint32_t gesture_id;
    float inference_time_ms;
  };

  auto infer(const std::vector<FeatureVector>& sequence) -> InferenceResult;

  auto get_gesture_name(std::uint32_t gesture_id) const -> const char*;

private:
  void load_model(const std::string& model_path, rknn_core_mask npu_core);
  void query_io_info();
  void flatten_features(const std::vector<FeatureVector>& sequence);
  auto run_inference() -> std::uint32_t;

  rknn_context ctx_{0};
  GestureLabelMap label_map_;
  std::vector<float> input_buffer_;
  std::uint32_t expected_sequence_length_{0};
  rknn_input_output_num io_num_{};
  std::vector<rknn_tensor_attr> input_attrs_;
  std::vector<rknn_tensor_attr> output_attrs_;
};

} // namespace signlang::signlang_det

#endif // SIGNLANG_EYES_EDGEAI_SIGNLANG_DET_SIGNLANG_MODEL_HPP
