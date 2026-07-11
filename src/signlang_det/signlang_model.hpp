#ifndef SIGNLANG_EYES_SIGNLANG_DET_SIGNLANG_MODEL_HPP
#define SIGNLANG_EYES_SIGNLANG_DET_SIGNLANG_MODEL_HPP

#include "gesture_pipeline.hpp"

#include "rknn_api.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace signlang::signlang_det {

  enum class CalibrationStatus : std::uint32_t { Uncalibrated = 0, Calibrated = 1 };

  struct GesturePrototype {
    std::uint32_t sample_id;
    std::vector<FrameEmbedding> frames;
    FrameEmbedding pooled;
    std::uint32_t valid_length;
    float quality;
    std::uint64_t captured_at;
  };

  struct GesturePrototypeSet {
    std::uint32_t gesture_id;
    std::string name;
    float dtw_threshold;
    float coarse_threshold;
    CalibrationStatus calibration;
    std::vector<GesturePrototype> samples;
  };

  class PrototypeStore {
  public:
    [[nodiscard]] auto gestures() const -> const std::vector<GesturePrototypeSet>&;
    [[nodiscard]] auto gesture_count() const -> std::size_t;
    [[nodiscard]] auto sample_count() const -> std::size_t;
    [[nodiscard]] auto gesture_name(std::uint32_t gesture_id) const -> const char*;
    void replace(std::vector<GesturePrototypeSet> gestures);

  private:
    std::vector<GesturePrototypeSet> gestures_;
    std::unordered_map<std::uint32_t, std::string> names_;
    std::size_t sample_count_{0};
  };

  struct MatcherOptions {
    std::uint32_t top_k{20};
    float dtw_window_ratio{0.20F};
    float global_dtw_threshold{0.8F};
    float global_coarse_threshold{0.5F};
    float margin_threshold{0.05F};
    bool use_coarse_threshold{true};
    bool confidence_mapping_enabled{false};
    float confidence_slope{-8.0F};
    float confidence_intercept{4.0F};
  };

  struct MatchCandidate {
    std::uint32_t gesture_id{0};
    std::uint32_t sample_id{0};
    float coarse_distance{0.0F};
    float dtw_distance{0.0F};
    float confidence{0.0F};
  };

  struct MatchResult {
    bool recognized{false};
    RejectionReason rejection_reason{RejectionReason::NoPrototypes};
    std::uint32_t gesture_id{0};
    float top1_dtw_distance{0.0F};
    float top2_dtw_distance{0.0F};
    float coarse_distance{0.0F};
    float distance_margin{0.0F};
    float applied_dtw_threshold{0.0F};
    float applied_coarse_threshold{0.0F};
    std::vector<MatchCandidate> candidates;
  };

  class SignlangModel {
  public:
    struct InferenceResult {
      MatchResult match;
      PackedSegment segment;
      float inference_time_ms{0.0F};
    };

    SignlangModel(const std::string& model_path, const std::string& prototypes_path, rknn_core_mask npu_core,
                  PreprocessingOptions preprocessing, MatcherOptions matcher, float min_keypoint_confidence);
    ~SignlangModel();

    SignlangModel(const SignlangModel&) = delete;
    auto operator=(const SignlangModel&) -> SignlangModel& = delete;

    [[nodiscard]] auto infer(const GestureSegment& segment) -> InferenceResult;
    [[nodiscard]] auto encode_recording(const std::vector<RecordedHandposeFrame>& frames) -> EncodedGesture;
    [[nodiscard]] auto dtw_distance(const std::vector<FrameEmbedding>& lhs,
                                    const std::vector<FrameEmbedding>& rhs) const -> float;
    [[nodiscard]] auto get_gesture_name(std::uint32_t gesture_id) const -> const char*;
    [[nodiscard]] auto expected_sequence_length() const -> std::uint32_t;
    [[nodiscard]] auto embedding_dim() const -> std::uint32_t;
    [[nodiscard]] auto loaded_gesture_count() const -> std::size_t;
    [[nodiscard]] auto loaded_sample_count() const -> std::size_t;
    void reload_prototypes(const std::string& prototypes_path);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
  };

} // namespace signlang::signlang_det

#endif
