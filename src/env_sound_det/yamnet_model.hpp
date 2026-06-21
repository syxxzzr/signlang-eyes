#ifndef SIGNLANG_EYES_ENV_SOUND_DET_YAMNET_MODEL_HPP
#define SIGNLANG_EYES_ENV_SOUND_DET_YAMNET_MODEL_HPP

#include "common/audio_ring_buffer.hpp"
#include "program_options.hpp"

#include "rknn_api.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace signlang::env_sound_det {

  using signlang::common::AudioWindow;

  constexpr std::uint32_t kMaxDetectedClasses = 32;

  struct YamnetInferenceResult {
    std::uint32_t model_input_sample_count;
    std::uint32_t score_frame_count;
    std::uint32_t detected_class_count;
    float inference_time_ms;
    std::array<EnvSoundClassScore, kMaxDetectedClasses> detected_classes;
  };

  class YamnetModel {
  public:
    YamnetModel(const std::string& model_path, const std::string& class_map_path, rknn_core_mask npu_core_mask,
                std::uint32_t rknn_priority_flag, float score_threshold);
    ~YamnetModel();

    YamnetModel(const YamnetModel&) = delete;
    auto operator=(const YamnetModel&) -> YamnetModel& = delete;
    YamnetModel(YamnetModel&&) = delete;
    auto operator=(YamnetModel&&) -> YamnetModel& = delete;

    auto infer(const AudioWindow& audio_window) -> YamnetInferenceResult;

  private:
    void query_model_io();
    void allocate_workspaces();
    void load_labels(const std::string& class_map_path);
    auto prepare_input(const AudioWindow& audio_window) -> const float*;
    auto post_process_scores(const float* scores) -> std::uint32_t;
    auto class_label(std::uint32_t class_index) const -> const std::string&;

    rknn_context context_;
    rknn_input_output_num io_num_;
    std::vector<rknn_tensor_attr> input_attrs_;
    std::vector<rknn_tensor_attr> output_attrs_;
    std::vector<float> input_samples_;
    std::vector<std::vector<float>> output_buffers_;
    std::vector<rknn_output> outputs_;
    std::vector<std::string> labels_;
    std::vector<float> mean_scores_;
    std::array<EnvSoundClassScore, kMaxDetectedClasses> detected_classes_;
    std::uint32_t scores_output_index_;
    std::uint32_t score_frame_count_;
    float score_threshold_;
  };

} // namespace signlang::env_sound_det

#endif // SIGNLANG_EYES_ENV_SOUND_DET_YAMNET_MODEL_HPP
