#ifndef SIGNLANG_EYES_SIGNLANG_DET_SIGNLANG_MODEL_HPP
#define SIGNLANG_EYES_SIGNLANG_DET_SIGNLANG_MODEL_HPP

#include "signlang_result.hpp"

#include "rknn_api.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace signlang::signlang_det {

  using EncodedSequence = std::vector<std::vector<float>>;

  struct GesturePrototype {
    std::uint32_t sample_id;
    EncodedSequence frames;
  };

  struct GesturePrototypeSet {
    std::uint32_t gesture_id;
    std::string name;
    std::vector<GesturePrototype> samples;
  };

  class PrototypeStore {
  public:
    static auto load(const std::string& path) -> PrototypeStore;

    auto gestures() const -> const std::vector<GesturePrototypeSet>&;
    auto gesture_count() const -> std::size_t;
    auto sample_count() const -> std::size_t;
    auto embedding_dim() const -> std::uint32_t;
    auto gesture_name(std::uint32_t gesture_id) const -> const char*;

  private:
    std::vector<GesturePrototypeSet> gestures_;
    std::unordered_map<std::uint32_t, std::string> names_by_id_;
    std::uint32_t embedding_dim_{0};
    std::size_t sample_count_{0};
  };

  class DtwMatcher {
  public:
    struct Candidate {
      std::uint32_t gesture_id;
      std::uint32_t sample_id;
      float distance;
      float confidence;
    };

    explicit DtwMatcher(float window_ratio);

    auto match(const EncodedSequence& query, const PrototypeStore& store) const -> std::vector<Candidate>;

  private:
    auto compute_distance(const EncodedSequence& query, const EncodedSequence& sample) const -> float;
    auto compute_frame_distance(const std::vector<float>& query_frame, const std::vector<float>& sample_frame) const
        -> float;
    auto compute_window(std::uint32_t query_length, std::uint32_t sample_length) const -> std::uint32_t;

    float window_ratio_;
  };

  class BilstmEncoder {
  public:
    BilstmEncoder(const std::string& model_path, rknn_core_mask npu_core, float motion_weight);

    BilstmEncoder(const BilstmEncoder&) = delete;
    auto operator=(const BilstmEncoder&) -> BilstmEncoder& = delete;
    BilstmEncoder(BilstmEncoder&&) = delete;
    auto operator=(BilstmEncoder&&) -> BilstmEncoder& = delete;

    ~BilstmEncoder();

    auto encode(const std::vector<FeatureVector>& sequence) -> EncodedSequence;
    auto sequence_length() const -> std::uint32_t;
    auto embedding_dim() const -> std::uint32_t;

  private:
    void load_model(const std::string& model_path, rknn_core_mask npu_core);
    void query_io_info();
    void flatten_features(const std::vector<FeatureVector>& sequence);

    rknn_context ctx_{0};
    rknn_input_output_num io_num_{};
    rknn_tensor_attr input_attr_{};
    rknn_tensor_attr output_attr_{};
    std::uint32_t expected_sequence_length_{0};
    std::uint32_t frame_embedding_dim_{0};
    std::vector<float> input_buffer_;
    float motion_weight_;
  };

  class SignlangModel {
  public:
    struct InferenceResult {
      bool recognized;
      std::uint32_t gesture_id;
      float inference_time_ms;
      float confidence;
      float second_confidence;
      float distance;
      std::vector<DtwMatcher::Candidate> candidates;
    };

    SignlangModel(const std::string& model_path, const std::string& prototypes_path, rknn_core_mask npu_core,
                  float motion_weight = 0.0F, float dtw_window_ratio = 0.5F);

    SignlangModel(const SignlangModel&) = delete;
    auto operator=(const SignlangModel&) -> SignlangModel& = delete;
    SignlangModel(SignlangModel&&) = delete;
    auto operator=(SignlangModel&&) -> SignlangModel& = delete;

    ~SignlangModel();

    auto infer(const std::vector<FeatureVector>& sequence) -> InferenceResult;
    auto get_gesture_name(std::uint32_t gesture_id) const -> const char*;
    auto expected_sequence_length() const -> std::uint32_t;

  private:
    std::unique_ptr<BilstmEncoder> encoder_;
    PrototypeStore prototypes_;
    DtwMatcher matcher_;
  };

} // namespace signlang::signlang_det

#endif // SIGNLANG_EYES_SIGNLANG_DET_SIGNLANG_MODEL_HPP
