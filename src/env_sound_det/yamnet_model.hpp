#ifndef SIGNLANG_EYES_ENV_SOUND_DET_YAMNET_MODEL_HPP
#define SIGNLANG_EYES_ENV_SOUND_DET_YAMNET_MODEL_HPP

#include "audio_ring_buffer.hpp"
#include "env_sound_result.hpp"
#include "program_options.hpp"

#include "rknn_api.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace signlang::env_sound_det {

  struct YamnetInferenceResult {
    std::uint32_t model_input_sample_count;
    std::uint32_t score_frame_count;
    std::uint32_t top_class_count;
    float inference_time_ms;
    std::array<EnvSoundClassScore, kMaxTopClassCount> top_classes;
  };

  class YamnetModel {
  public:
    YamnetModel(const std::string& model_path, const std::string& class_map_path, rknn_core_mask npu_core_mask,
                std::uint32_t rknn_priority_flag, std::uint32_t top_k);
    ~YamnetModel();

    YamnetModel(const YamnetModel&) = delete;
    auto operator=(const YamnetModel&) -> YamnetModel& = delete;
    YamnetModel(YamnetModel&&) = delete;
    auto operator=(YamnetModel&&) -> YamnetModel& = delete;

    auto infer(const AudioWindow& audio_window) -> YamnetInferenceResult;

  private:
    void query_model_io();
    void allocate_workspaces(std::uint32_t top_k);
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
    std::array<EnvSoundClassScore, kMaxTopClassCount> top_classes_;
    std::uint32_t scores_output_index_;
    std::uint32_t score_frame_count_;
    std::uint32_t top_k_;
  };

} // namespace signlang::env_sound_det

#endif // SIGNLANG_EYES_ENV_SOUND_DET_YAMNET_MODEL_HPP
