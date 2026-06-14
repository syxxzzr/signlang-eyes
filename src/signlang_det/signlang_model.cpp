#include "signlang_model.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace signlang::signlang_det {

auto load_gesture_labels(const std::string& path) -> GestureLabelMap {
  std::ifstream file(path);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open label map file: " + path);
  }

  GestureLabelMap label_map;
  std::string line;
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

void SignlangModel::load_model(const std::string& model_path, rknn_core_mask npu_core) {
  std::ifstream model_file(model_path, std::ios::binary | std::ios::ate);
  if (!model_file.is_open()) {
    throw std::runtime_error("Failed to open RKNN model file: " + model_path);
  }

  const auto model_size = model_file.tellg();
  model_file.seekg(0, std::ios::beg);

  std::vector<char> model_data(model_size);
  if (!model_file.read(model_data.data(), model_size)) {
    throw std::runtime_error("Failed to read RKNN model file: " + model_path);
  }

  auto ret = rknn_init(&ctx_, model_data.data(), model_size, 0, nullptr);
  if (ret != RKNN_SUCC) {
    throw std::runtime_error("rknn_init failed: " + std::to_string(ret));
  }

  ret = rknn_set_core_mask(ctx_, npu_core);
  if (ret != RKNN_SUCC) {
    std::cerr << "Warning: rknn_set_core_mask failed: " << ret << std::endl;
  }
}

void SignlangModel::query_io_info() {
  auto ret = rknn_query(ctx_, RKNN_QUERY_IN_OUT_NUM, &io_num_, sizeof(io_num_));
  if (ret != RKNN_SUCC) {
    throw std::runtime_error("rknn_query IN_OUT_NUM failed: " + std::to_string(ret));
  }

  if (io_num_.n_input != 1 || io_num_.n_output != 1) {
    throw std::runtime_error("Model must have exactly 1 input and 1 output");
  }

  input_attrs_.resize(io_num_.n_input);
  output_attrs_.resize(io_num_.n_output);

  input_attrs_[0].index = 0;
  ret = rknn_query(ctx_, RKNN_QUERY_INPUT_ATTR, &input_attrs_[0], sizeof(rknn_tensor_attr));
  if (ret != RKNN_SUCC) {
    throw std::runtime_error("rknn_query INPUT_ATTR failed: " + std::to_string(ret));
  }

  output_attrs_[0].index = 0;
  ret = rknn_query(ctx_, RKNN_QUERY_OUTPUT_ATTR, &output_attrs_[0], sizeof(rknn_tensor_attr));
  if (ret != RKNN_SUCC) {
    throw std::runtime_error("rknn_query OUTPUT_ATTR failed: " + std::to_string(ret));
  }

  const auto& input_attr = input_attrs_[0];
  if (input_attr.n_dims != 3) {
    throw std::runtime_error("Input tensor must be 3D [batch, seq_len, features]");
  }

  expected_sequence_length_ = input_attr.dims[1];
  const auto expected_feature_dim = input_attr.dims[2];

  if (expected_feature_dim != kFeatureDim) {
    throw std::runtime_error("Model expects " + std::to_string(expected_feature_dim) +
                             " features, but extractor produces " + std::to_string(kFeatureDim));
  }

  input_buffer_.resize(expected_sequence_length_ * kFeatureDim);
}

SignlangModel::SignlangModel(const std::string& model_path,
                             const std::string& label_map_path,
                             rknn_core_mask npu_core)
  : label_map_(load_gesture_labels(label_map_path))
{
  load_model(model_path, npu_core);
  query_io_info();
}

SignlangModel::~SignlangModel() {
  if (ctx_ != 0) {
    rknn_destroy(ctx_);
  }
}

void SignlangModel::flatten_features(const std::vector<FeatureVector>& sequence) {
  if (sequence.size() != expected_sequence_length_) {
    throw std::runtime_error("Sequence length mismatch: expected " +
                             std::to_string(expected_sequence_length_) +
                             ", got " + std::to_string(sequence.size()));
  }

  std::size_t offset = 0;
  for (const auto& frame : sequence) {
    for (const auto& kp : frame.features) {
      input_buffer_[offset++] = kp.normalized_x;
      input_buffer_[offset++] = kp.normalized_y;
      input_buffer_[offset++] = kp.velocity_magnitude;
    }
  }
}

auto SignlangModel::run_inference() -> std::uint32_t {
  rknn_input inputs[1];
  std::memset(inputs, 0, sizeof(inputs));
  inputs[0].index = 0;
  inputs[0].type = RKNN_TENSOR_FLOAT32;
  inputs[0].size = input_buffer_.size() * sizeof(float);
  inputs[0].fmt = RKNN_TENSOR_NCHW;
  inputs[0].buf = input_buffer_.data();

  auto ret = rknn_inputs_set(ctx_, 1, inputs);
  if (ret != RKNN_SUCC) {
    throw std::runtime_error("rknn_inputs_set failed: " + std::to_string(ret));
  }

  ret = rknn_run(ctx_, nullptr);
  if (ret != RKNN_SUCC) {
    throw std::runtime_error("rknn_run failed: " + std::to_string(ret));
  }

  rknn_output outputs[1];
  std::memset(outputs, 0, sizeof(outputs));
  outputs[0].want_float = 1;

  ret = rknn_outputs_get(ctx_, 1, outputs, nullptr);
  if (ret != RKNN_SUCC) {
    throw std::runtime_error("rknn_outputs_get failed: " + std::to_string(ret));
  }

  const auto* logits = static_cast<const float*>(outputs[0].buf);
  const auto num_classes = outputs[0].size / sizeof(float);

  std::uint32_t max_class = 0;
  float max_score = logits[0];
  for (std::uint32_t i = 1; i < num_classes; ++i) {
    if (logits[i] > max_score) {
      max_score = logits[i];
      max_class = i;
    }
  }

  rknn_outputs_release(ctx_, 1, outputs);

  return max_class;
}

auto SignlangModel::infer(const std::vector<FeatureVector>& sequence)
  -> InferenceResult
{
  flatten_features(sequence);

  const auto start_time = std::chrono::steady_clock::now();
  const auto gesture_id = run_inference();
  const auto end_time = std::chrono::steady_clock::now();

  const auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
    end_time - start_time).count();
  const float inference_time_ms = duration_ns / 1e6f;

  return InferenceResult{gesture_id, inference_time_ms};
}

auto SignlangModel::get_gesture_name(std::uint32_t gesture_id) const -> const char* {
  if (gesture_id >= label_map_.labels.size()) {
    return "unknown";
  }
  return label_map_.labels[gesture_id].c_str();
}

} // namespace signlang::signlang_det
