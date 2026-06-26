#include "gesture_encoder.hpp"

#include "spdlog/spdlog.h"

#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace signlang::signlang_manager {
  namespace {

    class RknnOutputReleaseGuard {
    public:
      RknnOutputReleaseGuard(rknn_context context, std::uint32_t output_count, rknn_output* outputs) :
          context_{context}, output_count_{output_count}, outputs_{outputs} {}

      RknnOutputReleaseGuard(const RknnOutputReleaseGuard&) = delete;
      auto operator=(const RknnOutputReleaseGuard&) -> RknnOutputReleaseGuard& = delete;
      RknnOutputReleaseGuard(RknnOutputReleaseGuard&&) = delete;
      auto operator=(RknnOutputReleaseGuard&&) -> RknnOutputReleaseGuard& = delete;

      ~RknnOutputReleaseGuard() { static_cast<void>(rknn_outputs_release(context_, output_count_, outputs_)); }

    private:
      rknn_context context_;
      std::uint32_t output_count_;
      rknn_output* outputs_;
    };

  } // namespace

  GestureEncoder::GestureEncoder(const std::string& model_path, rknn_core_mask npu_core, float motion_weight) :
      motion_weight_{motion_weight} {
    load_model(model_path, npu_core);
    query_io_info();
  }

  GestureEncoder::~GestureEncoder() {
    if (ctx_ != 0) {
      rknn_destroy(ctx_);
    }
  }

  auto GestureEncoder::sequence_length() const -> std::uint32_t { return expected_sequence_length_; }

  auto GestureEncoder::embedding_dim() const -> std::uint32_t { return frame_embedding_dim_; }

  void GestureEncoder::load_model(const std::string& model_path, rknn_core_mask npu_core) {
    auto model_file = std::ifstream{model_path, std::ios::binary | std::ios::ate};
    if (!model_file.is_open()) {
      throw std::runtime_error("Failed to open RKNN gesture encoder model file: " + model_path);
    }

    const auto model_size = model_file.tellg();
    if (model_size <= 0) {
      throw std::runtime_error("RKNN gesture encoder model file is empty: " + model_path);
    }
    model_file.seekg(0, std::ios::beg);

    auto model_data = std::vector<char>(static_cast<std::size_t>(model_size));
    if (!model_file.read(model_data.data(), model_size)) {
      throw std::runtime_error("Failed to read RKNN gesture encoder model file: " + model_path);
    }

    auto ret = rknn_init(&ctx_, model_data.data(), static_cast<std::uint32_t>(model_data.size()), 0, nullptr);
    if (ret != RKNN_SUCC) {
      throw std::runtime_error("rknn_init failed for gesture encoder, ret=" + std::to_string(ret));
    }

    ret = rknn_set_core_mask(ctx_, npu_core);
    if (ret != RKNN_SUCC) {
      spdlog::warn("rknn_set_core_mask failed for gesture encoder, ret={}", ret);
    }
  }

  void GestureEncoder::query_io_info() {
    auto ret = rknn_query(ctx_, RKNN_QUERY_IN_OUT_NUM, &io_num_, sizeof(io_num_));
    if (ret != RKNN_SUCC) {
      throw std::runtime_error("rknn_query IN_OUT_NUM failed for gesture encoder, ret=" + std::to_string(ret));
    }
    if (io_num_.n_input != 1 || io_num_.n_output != 1) {
      throw std::runtime_error("Gesture encoder expects 1 input and 1 output, got " + std::to_string(io_num_.n_input) +
                               " inputs and " + std::to_string(io_num_.n_output) + " outputs");
    }

    std::memset(&input_attr_, 0, sizeof(input_attr_));
    input_attr_.index = 0;
    ret = rknn_query(ctx_, RKNN_QUERY_INPUT_ATTR, &input_attr_, sizeof(input_attr_));
    if (ret != RKNN_SUCC) {
      throw std::runtime_error("rknn_query INPUT_ATTR failed for gesture encoder, ret=" + std::to_string(ret));
    }
    if (input_attr_.n_dims != 3) {
      throw std::runtime_error("Gesture encoder expects 3D input tensor");
    }

    expected_sequence_length_ = input_attr_.dims[1];
    const auto expected_feature_dim = input_attr_.dims[2];
    if (expected_feature_dim != kFeatureDim) {
      throw std::runtime_error("Gesture encoder feature dimension mismatch: model expects " +
                               std::to_string(expected_feature_dim) + ", manager has " + std::to_string(kFeatureDim));
    }

    std::memset(&output_attr_, 0, sizeof(output_attr_));
    output_attr_.index = 0;
    ret = rknn_query(ctx_, RKNN_QUERY_OUTPUT_ATTR, &output_attr_, sizeof(output_attr_));
    if (ret != RKNN_SUCC) {
      throw std::runtime_error("rknn_query OUTPUT_ATTR failed for gesture encoder, ret=" + std::to_string(ret));
    }
    if (output_attr_.n_dims != 3) {
      throw std::runtime_error("Gesture encoder expects 3D output tensor");
    }

    frame_embedding_dim_ = output_attr_.dims[2];
    input_buffer_.resize(static_cast<std::size_t>(expected_sequence_length_) * kFeatureDim);
  }

  void GestureEncoder::flatten_features(const std::vector<FeatureVector>& sequence) {
    if (sequence.size() != expected_sequence_length_) {
      throw std::runtime_error("Gesture encoder sequence length mismatch: expected " +
                               std::to_string(expected_sequence_length_) + ", got " + std::to_string(sequence.size()));
    }

    auto offset = std::size_t{0};
    for (const auto& frame : sequence) {
      for (const auto& hand : frame.hands) {
        for (const auto& kp : hand.features) {
          input_buffer_[offset++] = kp.normalized_x;
          input_buffer_[offset++] = kp.normalized_y;
          input_buffer_[offset++] = kp.normalized_z;
          input_buffer_[offset++] = kp.velocity_magnitude * motion_weight_;
        }
      }
    }
  }

  auto GestureEncoder::encode(const std::vector<FeatureVector>& sequence) -> EncodedSequence {
    flatten_features(sequence);

    auto inputs = std::array<rknn_input, 1>{};
    std::memset(inputs.data(), 0, sizeof(inputs));
    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_FLOAT32;
    inputs[0].size = input_buffer_.size() * sizeof(float);
    inputs[0].fmt = RKNN_TENSOR_NCHW;
    inputs[0].buf = input_buffer_.data();

    auto ret = rknn_inputs_set(ctx_, 1, inputs.data());
    if (ret != RKNN_SUCC) {
      throw std::runtime_error("rknn_inputs_set failed for gesture encoder, ret=" + std::to_string(ret));
    }

    ret = rknn_run(ctx_, nullptr);
    if (ret != RKNN_SUCC) {
      throw std::runtime_error("rknn_run failed for gesture encoder, ret=" + std::to_string(ret));
    }

    auto outputs = std::array<rknn_output, 1>{};
    std::memset(outputs.data(), 0, sizeof(outputs));
    outputs[0].want_float = 1;

    ret = rknn_outputs_get(ctx_, 1, outputs.data(), nullptr);
    if (ret != RKNN_SUCC) {
      throw std::runtime_error("rknn_outputs_get failed for gesture encoder, ret=" + std::to_string(ret));
    }
    const auto guard = RknnOutputReleaseGuard{ctx_, 1, outputs.data()};

    const auto* output_data = static_cast<const float*>(outputs[0].buf);
    const auto output_size = outputs[0].size / sizeof(float);
    const auto expected_output_size = expected_sequence_length_ * frame_embedding_dim_;
    if (output_size != expected_output_size) {
      throw std::runtime_error("Gesture encoder output size mismatch");
    }

    auto encoded_frames = EncodedSequence(expected_sequence_length_);
    for (std::uint32_t frame_index = 0; frame_index < expected_sequence_length_; ++frame_index) {
      encoded_frames[frame_index].assign(output_data + frame_index * frame_embedding_dim_,
                                         output_data + (frame_index + 1) * frame_embedding_dim_);
    }
    return encoded_frames;
  }

} // namespace signlang::signlang_manager
