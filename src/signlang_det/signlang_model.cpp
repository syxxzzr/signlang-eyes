#include "signlang_model.hpp"

#include "spdlog/spdlog.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace signlang::signlang_det {
  namespace {

    class RknnOutputReleaseGuard {
    public:
      RknnOutputReleaseGuard(rknn_context context, std::uint32_t output_count, rknn_output* outputs)
        : context_{context}, output_count_{output_count}, outputs_{outputs}, active_{true} {}

      RknnOutputReleaseGuard(const RknnOutputReleaseGuard&) = delete;
      auto operator=(const RknnOutputReleaseGuard&) -> RknnOutputReleaseGuard& = delete;
      RknnOutputReleaseGuard(RknnOutputReleaseGuard&&) = delete;
      auto operator=(RknnOutputReleaseGuard&&) -> RknnOutputReleaseGuard& = delete;

      ~RknnOutputReleaseGuard() {
        if (active_) {
          static_cast<void>(rknn_outputs_release(context_, output_count_, outputs_));
        }
      }

      auto release() -> int {
        const auto result = rknn_outputs_release(context_, output_count_, outputs_);
        active_ = false;
        return result;
      }

    private:
      rknn_context context_;
      std::uint32_t output_count_;
      rknn_output* outputs_;
      bool active_;
    };

    auto load_gesture_labels_impl(const std::string& path) -> GestureLabelMap {
      std::ifstream file{path};
      if (!file.is_open()) {
        throw std::runtime_error("Failed to open label map file: " + path);
      }

      auto label_map = GestureLabelMap{};
      auto line = std::string{};
      while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }
        if (!line.empty()) {
          label_map.labels.push_back(line);
        }
      }

      if (label_map.labels.empty()) {
        throw std::runtime_error("Label map file is empty: " + path);
      }

      return label_map;
    }

    auto load_gesture_prototypes_impl(const std::string& path) 
      -> std::unordered_map<std::uint32_t, GesturePrototype>
    {
      std::ifstream file{path, std::ios::binary};
      if (!file.is_open()) {
        throw std::runtime_error("Failed to open prototypes file: " + path);
      }

      auto prototypes = std::unordered_map<std::uint32_t, GesturePrototype>{};
      
      std::uint32_t num_gestures = 0;
      file.read(reinterpret_cast<char*>(&num_gestures), sizeof(num_gestures));
      
      for (std::uint32_t i = 0; i < num_gestures; ++i) {
        std::uint32_t gesture_id = 0;
        std::uint32_t num_samples = 0;
        file.read(reinterpret_cast<char*>(&gesture_id), sizeof(gesture_id));
        file.read(reinterpret_cast<char*>(&num_samples), sizeof(num_samples));
        
        auto prototype = GesturePrototype{};
        
        for (std::uint32_t j = 0; j < num_samples; ++j) {
          std::uint32_t num_frames = 0;
          std::uint32_t embedding_dim = 0;
          file.read(reinterpret_cast<char*>(&num_frames), sizeof(num_frames));
          file.read(reinterpret_cast<char*>(&embedding_dim), sizeof(embedding_dim));
          
          auto encoded_sample = std::vector<std::vector<float>>(num_frames);
          for (auto& frame : encoded_sample) {
            frame.resize(embedding_dim);
            file.read(reinterpret_cast<char*>(frame.data()), embedding_dim * sizeof(float));
          }
          
          prototype.encoded_frames.insert(
            prototype.encoded_frames.end(),
            encoded_sample.begin(),
            encoded_sample.end()
          );
        }
        
        prototypes[gesture_id] = std::move(prototype);
      }
      
      return prototypes;
    }

  } // namespace
  
  auto load_gesture_labels(const std::string& path) -> GestureLabelMap {
    return load_gesture_labels_impl(path);
  }

  auto load_gesture_prototypes(const std::string& path) 
    -> std::unordered_map<std::uint32_t, GesturePrototype>
  {
    return load_gesture_prototypes_impl(path);
  }
  // Constructor and model loading
  
  SignlangModel::SignlangModel(const std::string& model_path,
                               const std::string& label_map_path,
                               const std::string& prototypes_path,
                               rknn_core_mask npu_core,
                               float motion_weight,
                               float dtw_window_ratio)
    : motion_weight_{motion_weight}, dtw_window_ratio_{dtw_window_ratio}
  {
    labels_ = load_gesture_labels(label_map_path);
    prototypes_ = load_gesture_prototypes(prototypes_path);
    load_model(model_path, npu_core);
    query_io_info();
    prepare_motion_weighting();
  }

  SignlangModel::~SignlangModel() {
    if (ctx_ != 0) {
      rknn_destroy(ctx_);
    }
  }

  void SignlangModel::load_model(const std::string& model_path, rknn_core_mask npu_core) {
    auto model_file = std::ifstream{model_path, std::ios::binary | std::ios::ate};
    if (!model_file.is_open()) {
      throw std::runtime_error("Failed to open RKNN model file: " + model_path);
    }

    const auto model_size = model_file.tellg();
    model_file.seekg(0, std::ios::beg);

    auto model_data = std::vector<char>(model_size);
    if (!model_file.read(model_data.data(), model_size)) {
      throw std::runtime_error("Failed to read RKNN model file: " + model_path);
    }

    auto ret = rknn_init(&ctx_, model_data.data(), model_size, 0, nullptr);
    if (ret != RKNN_SUCC) {
      throw std::runtime_error("rknn_init failed, ret=" + std::to_string(ret));
    }

    ret = rknn_set_core_mask(ctx_, npu_core);
    if (ret != RKNN_SUCC) {
      spdlog::warn("rknn_set_core_mask failed, ret={}", ret);
    }
  }

  void SignlangModel::query_io_info() {
    auto ret = rknn_query(ctx_, RKNN_QUERY_IN_OUT_NUM, &io_num_, sizeof(io_num_));
    if (ret != RKNN_SUCC) {
      throw std::runtime_error("rknn_query IN_OUT_NUM failed, ret=" + std::to_string(ret));
    }

    if (io_num_.n_input != 1 || io_num_.n_output != 1) {
      throw std::runtime_error("Expected 1 input and 1 output, got " +
                               std::to_string(io_num_.n_input) + " inputs and " +
                               std::to_string(io_num_.n_output) + " outputs");
    }

    std::memset(&input_attr_, 0, sizeof(input_attr_));
    input_attr_.index = 0;
    ret = rknn_query(ctx_, RKNN_QUERY_INPUT_ATTR, &input_attr_, sizeof(input_attr_));
    if (ret != RKNN_SUCC) {
      throw std::runtime_error("rknn_query INPUT_ATTR failed, ret=" + std::to_string(ret));
    }

    if (input_attr_.n_dims != 3) {
      throw std::runtime_error("Expected 3D input tensor [batch, seq_len, features], got " +
                               std::to_string(input_attr_.n_dims) + " dimensions");
    }

    expected_sequence_length_ = input_attr_.dims[1];
    const auto expected_feature_dim = input_attr_.dims[2];
    if (expected_feature_dim != kFeatureDim) {
      throw std::runtime_error("Feature dimension mismatch: model expects " +
                               std::to_string(expected_feature_dim) +
                               ", but kFeatureDim=" + std::to_string(kFeatureDim));
    }

    std::memset(&output_attr_, 0, sizeof(output_attr_));
    output_attr_.index = 0;
    ret = rknn_query(ctx_, RKNN_QUERY_OUTPUT_ATTR, &output_attr_, sizeof(output_attr_));
    if (ret != RKNN_SUCC) {
      throw std::runtime_error("rknn_query OUTPUT_ATTR failed, ret=" + std::to_string(ret));
    }

    if (output_attr_.n_dims != 3) {
      throw std::runtime_error("Expected 3D output tensor [batch, seq_len, embedding], got " +
                               std::to_string(output_attr_.n_dims) + " dimensions");
    }

    frame_embedding_dim_ = output_attr_.dims[2];

    const auto input_size = expected_sequence_length_ * kFeatureDim;
    input_buffer_.resize(input_size);
  }

  void SignlangModel::prepare_motion_weighting() {
    motion_indices_.clear();
    const auto hand_dim = kKeypointCount * 3;

    for (std::uint32_t hand_idx = 0; hand_idx < kMaxHandCount; ++hand_idx) {
      const auto hand_offset = hand_idx * hand_dim;
      for (std::uint32_t kp_idx = 0; kp_idx < kKeypointCount; ++kp_idx) {
        const auto velocity_idx = hand_offset + kp_idx * 3 + 2;
        if (velocity_idx < kFeatureDim) {
          motion_indices_.push_back(velocity_idx);
        }
      }
    }
  }

  auto SignlangModel::get_gesture_name(std::uint32_t gesture_id) const -> const char* {
    if (gesture_id < labels_.labels.size()) {
      return labels_.labels[gesture_id].c_str();
    }
    return "unknown";
  }
  // Feature processing and RKNN encoding
  
  void SignlangModel::flatten_features(const std::vector<FeatureVector>& sequence) {
    if (sequence.size() != expected_sequence_length_) {
      throw std::runtime_error("Sequence length mismatch: expected " +
                               std::to_string(expected_sequence_length_) +
                               ", got " + std::to_string(sequence.size()));
    }

    auto offset = std::size_t{0};
    for (const auto& frame : sequence) {
      for (const auto& hand : frame.hands) {
        for (const auto& kp : hand.features) {
          input_buffer_[offset++] = kp.normalized_x;
          input_buffer_[offset++] = kp.normalized_y;
          input_buffer_[offset++] = kp.velocity_magnitude * motion_weight_;
        }
      }
    }
  }

  auto SignlangModel::encode_sequence() -> std::vector<std::vector<float>> {
    auto inputs = std::array<rknn_input, 1>{};
    std::memset(inputs.data(), 0, sizeof(inputs));
    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_FLOAT32;
    inputs[0].size = input_buffer_.size() * sizeof(float);
    inputs[0].fmt = RKNN_TENSOR_NCHW;
    inputs[0].buf = input_buffer_.data();

    auto ret = rknn_inputs_set(ctx_, 1, inputs.data());
    if (ret != RKNN_SUCC) {
      throw std::runtime_error("rknn_inputs_set failed, ret=" + std::to_string(ret));
    }

    ret = rknn_run(ctx_, nullptr);
    if (ret != RKNN_SUCC) {
      throw std::runtime_error("rknn_run failed, ret=" + std::to_string(ret));
    }

    auto outputs = std::array<rknn_output, 1>{};
    std::memset(outputs.data(), 0, sizeof(outputs));
    outputs[0].want_float = 1;

    ret = rknn_outputs_get(ctx_, 1, outputs.data(), nullptr);
    if (ret != RKNN_SUCC) {
      throw std::runtime_error("rknn_outputs_get failed, ret=" + std::to_string(ret));
    }

    const auto guard = RknnOutputReleaseGuard{ctx_, 1, outputs.data()};

    const auto* output_data = static_cast<const float*>(outputs[0].buf);
    const auto output_size = outputs[0].size / sizeof(float);
    const auto expected_output_size = expected_sequence_length_ * frame_embedding_dim_;

    if (output_size != expected_output_size) {
      throw std::runtime_error("Output size mismatch: expected " +
                               std::to_string(expected_output_size) +
                               ", got " + std::to_string(output_size));
    }

    auto encoded_frames = std::vector<std::vector<float>>(expected_sequence_length_);
    for (std::uint32_t i = 0; i < expected_sequence_length_; ++i) {
      encoded_frames[i].assign(
        output_data + i * frame_embedding_dim_,
        output_data + (i + 1) * frame_embedding_dim_
      );
    }

    return encoded_frames;
  }
  // DTW algorithm implementation
  
  auto SignlangModel::compute_frame_distance(const std::vector<float>& query_frame,
                                             const std::vector<float>& sample_frame) const -> float {
    if (query_frame.size() != sample_frame.size()) {
      return std::numeric_limits<float>::infinity();
    }

    auto sum_sq_diff = 0.0F;
    for (std::size_t i = 0; i < query_frame.size(); ++i) {
      const auto diff = query_frame[i] - sample_frame[i];
      sum_sq_diff += diff * diff;
    }

    return std::sqrt(sum_sq_diff / static_cast<float>(query_frame.size()));
  }

  auto SignlangModel::compute_dtw_window(std::uint32_t query_length,
                                         std::uint32_t sample_length) const -> std::uint32_t {
    const auto max_length = std::max(query_length, sample_length);
    
    if (dtw_window_ratio_ >= 1.0F) {
      return max_length;
    }

    const auto ratio_window = static_cast<std::uint32_t>(
      std::round(static_cast<float>(max_length) * dtw_window_ratio_)
    );
    const auto length_diff = query_length > sample_length 
      ? query_length - sample_length 
      : sample_length - query_length;

    return std::max({length_diff, ratio_window, 1U});
  }

  auto SignlangModel::compute_dtw_distance(const std::vector<std::vector<float>>& query,
                                           const std::vector<std::vector<float>>& sample) const -> float {
    const auto query_length = static_cast<std::uint32_t>(query.size());
    const auto sample_length = static_cast<std::uint32_t>(sample.size());
    const auto window = compute_dtw_window(query_length, sample_length);

    auto prev_cost = std::vector<float>(sample_length + 1, std::numeric_limits<float>::infinity());
    auto prev_steps = std::vector<std::uint32_t>(sample_length + 1, 0);
    prev_cost[0] = 0.0F;

    for (std::uint32_t i = 1; i <= query_length; ++i) {
      auto curr_cost = std::vector<float>(sample_length + 1, std::numeric_limits<float>::infinity());
      auto curr_steps = std::vector<std::uint32_t>(sample_length + 1, 0);

      const auto j_start = (i > window) ? (i - window) : 1U;
      const auto j_end = std::min(sample_length, i + window);

      for (auto j = j_start; j <= j_end; ++j) {
        const auto candidates = std::array<std::pair<float, std::uint32_t>, 3>{{
          {prev_cost[j], prev_steps[j]},
          {curr_cost[j - 1], curr_steps[j - 1]},
          {prev_cost[j - 1], prev_steps[j - 1]}
        }};

        const auto best = *std::min_element(
          candidates.begin(), 
          candidates.end(),
          [](const auto& a, const auto& b) { return a.first < b.first; }
        );

        const auto frame_cost = compute_frame_distance(query[i - 1], sample[j - 1]);
        curr_cost[j] = best.first + frame_cost;
        curr_steps[j] = best.second + 1;
      }

      prev_cost = std::move(curr_cost);
      prev_steps = std::move(curr_steps);
    }

    if (!std::isfinite(prev_cost[sample_length]) || prev_steps[sample_length] == 0) {
      return std::numeric_limits<float>::infinity();
    }

    return prev_cost[sample_length] / static_cast<float>(prev_steps[sample_length]);
  }
  // DTW matching and inference
  
  auto SignlangModel::dtw_match(const std::vector<std::vector<float>>& query_frames) -> InferenceResult {
    auto scores = std::vector<std::pair<std::uint32_t, float>>{};

    for (const auto& [gesture_id, prototype] : prototypes_) {
      auto min_distance = std::numeric_limits<float>::infinity();
      
      const auto distance = compute_dtw_distance(query_frames, prototype.encoded_frames);
      if (distance < min_distance) {
        min_distance = distance;
      }

      const auto confidence = 1.0F / (1.0F + min_distance);
      scores.emplace_back(gesture_id, confidence);
    }

    if (scores.empty()) {
      return InferenceResult{
        .gesture_id = 0,
        .inference_time_ms = 0.0F,
        .confidence = 0.0F,
        .second_confidence = 0.0F
      };
    }

    std::sort(scores.begin(), scores.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    const auto best_gesture_id = scores[0].first;
    const auto best_confidence = scores[0].second;
    const auto second_confidence = (scores.size() > 1) ? scores[1].second : 0.0F;

    return InferenceResult{
      .gesture_id = best_gesture_id,
      .inference_time_ms = 0.0F,
      .confidence = best_confidence,
      .second_confidence = second_confidence
    };
  }

  auto SignlangModel::infer(const std::vector<FeatureVector>& sequence) -> InferenceResult {
    const auto start_time = std::chrono::steady_clock::now();

    flatten_features(sequence);
    const auto encoded_frames = encode_sequence();
    auto result = dtw_match(encoded_frames);

    const auto end_time = std::chrono::steady_clock::now();
    const auto elapsed_ms = std::chrono::duration<float, std::milli>(end_time - start_time).count();
    result.inference_time_ms = elapsed_ms;

    return result;
  }

} // namespace signlang::signlang_det
