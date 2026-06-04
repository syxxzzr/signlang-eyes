#include "yamnet_model.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace signlang::env_sound_det {
  namespace {

    class RknnOutputReleaseGuard {
    public:
      RknnOutputReleaseGuard(rknn_context context, std::uint32_t output_count, rknn_output* outputs) :
          context_{context}, output_count_{output_count}, outputs_{outputs}, active_{true} {}

      RknnOutputReleaseGuard(const RknnOutputReleaseGuard&) = delete;
      auto operator=(const RknnOutputReleaseGuard&) -> RknnOutputReleaseGuard& = delete;

      RknnOutputReleaseGuard(RknnOutputReleaseGuard&&) = delete;
      auto operator=(RknnOutputReleaseGuard&&) -> RknnOutputReleaseGuard& = delete;

      ~RknnOutputReleaseGuard() {
        if (active_) {
          (void)rknn_outputs_release(context_, output_count_, outputs_);
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

    auto rknn_error(const std::string& context, int error_code) -> std::runtime_error {
      return std::runtime_error(context + ": ret=" + std::to_string(error_code));
    }

    void clear_label(std::array<char, kMaxClassLabelLength>& label) { label.fill('\0'); }

    void copy_label(const std::string& source, std::array<char, kMaxClassLabelLength>& destination) {
      clear_label(destination);
      const auto copy_size = std::min(source.size(), destination.size() - 1);
      std::copy_n(source.data(), copy_size, destination.data());
    }

    auto trim_leading_spaces(std::string value) -> std::string {
      const auto first = value.find_first_not_of(" \t");
      if (first == std::string::npos) {
        return {};
      }

      return value.substr(first);
    }

  } // namespace

  YamnetModel::YamnetModel(const std::string& model_path, const std::string& class_map_path,
                           rknn_core_mask npu_core_mask, std::uint32_t rknn_priority_flag, std::uint32_t top_k) :
      context_{0}, io_num_{}, scores_output_index_{0}, score_frame_count_{0}, top_k_{0} {
    load_labels(class_map_path);

    auto* model_path_buffer = const_cast<char*>(model_path.c_str());
    const auto init_result = rknn_init(&context_, model_path_buffer, 0, rknn_priority_flag, nullptr);
    if (init_result < 0) {
      throw rknn_error("Failed to initialize RKNN YAMNet model " + model_path, init_result);
    }

    const auto core_result = rknn_set_core_mask(context_, npu_core_mask);
    if (core_result < 0) {
      throw rknn_error("Failed to set RKNN NPU core mask", core_result);
    }

    query_model_io();
    allocate_workspaces(top_k);
  }

  YamnetModel::~YamnetModel() {
    if (context_ != 0) {
      rknn_destroy(context_);
      context_ = 0;
    }
  }

  auto YamnetModel::infer(const AudioWindow& audio_window) -> YamnetInferenceResult {
    const auto* input_buffer = prepare_input(audio_window);

    rknn_input input{};
    input.index = 0;
    input.buf = const_cast<float*>(input_buffer);
    input.size = static_cast<std::uint32_t>(input_attrs_[0].n_elems * sizeof(float));
    input.pass_through = 0;
    input.type = RKNN_TENSOR_FLOAT32;
    input.fmt = input_attrs_[0].fmt;

    const auto start_time = std::chrono::steady_clock::now();

    auto result = rknn_inputs_set(context_, 1, &input);
    if (result < 0) {
      throw rknn_error("Failed to set RKNN YAMNet input", result);
    }

    result = rknn_run(context_, nullptr);
    if (result < 0) {
      throw rknn_error("Failed to run RKNN YAMNet inference", result);
    }

    result = rknn_outputs_get(context_, io_num_.n_output, outputs_.data(), nullptr);
    if (result < 0) {
      throw rknn_error("Failed to get RKNN YAMNet outputs", result);
    }
    RknnOutputReleaseGuard output_release_guard{context_, io_num_.n_output, outputs_.data()};

    const auto* score_buffer = output_buffers_[scores_output_index_].data();
    const auto top_class_count = post_process_scores(score_buffer);

    result = output_release_guard.release();
    if (result < 0) {
      throw rknn_error("Failed to release RKNN YAMNet outputs", result);
    }

    const auto end_time = std::chrono::steady_clock::now();
    const auto inference_time_ms =
        std::chrono::duration<float, std::milli>(end_time - start_time).count();

    return YamnetInferenceResult{
        .model_input_sample_count = input_attrs_[0].n_elems,
        .score_frame_count = score_frame_count_,
        .top_class_count = top_class_count,
        .inference_time_ms = inference_time_ms,
        .top_classes = top_classes_,
    };
  }

  void YamnetModel::query_model_io() {
    auto result = rknn_query(context_, RKNN_QUERY_IN_OUT_NUM, &io_num_, sizeof(io_num_));
    if (result != RKNN_SUCC) {
      throw rknn_error("Failed to query RKNN YAMNet input/output count", result);
    }

    if (io_num_.n_input != 1 || io_num_.n_output == 0) {
      throw std::runtime_error("Unexpected RKNN YAMNet input/output count");
    }

    input_attrs_.resize(io_num_.n_input);
    for (std::uint32_t input_index = 0; input_index < io_num_.n_input; ++input_index) {
      input_attrs_[input_index] = {};
      input_attrs_[input_index].index = input_index;
      result = rknn_query(context_, RKNN_QUERY_INPUT_ATTR, &input_attrs_[input_index], sizeof(rknn_tensor_attr));
      if (result != RKNN_SUCC) {
        throw rknn_error("Failed to query RKNN YAMNet input tensor", result);
      }
    }

    output_attrs_.resize(io_num_.n_output);
    for (std::uint32_t output_index = 0; output_index < io_num_.n_output; ++output_index) {
      output_attrs_[output_index] = {};
      output_attrs_[output_index].index = output_index;
      result = rknn_query(context_, RKNN_QUERY_OUTPUT_ATTR, &output_attrs_[output_index], sizeof(rknn_tensor_attr));
      if (result != RKNN_SUCC) {
        throw rknn_error("Failed to query RKNN YAMNet output tensor", result);
      }
    }
  }

  void YamnetModel::allocate_workspaces(std::uint32_t top_k) {
    if (top_k == 0 || top_k > kMaxTopClassCount) {
      throw std::runtime_error("YAMNet top-k must be between 1 and " + std::to_string(kMaxTopClassCount));
    }

    top_k_ = top_k;

    if (input_attrs_[0].n_elems == 0) {
      throw std::runtime_error("RKNN YAMNet input tensor has no elements");
    }

    output_buffers_.resize(output_attrs_.size());
    outputs_.resize(output_attrs_.size());

    const auto label_count = labels_.size();
    scores_output_index_ = static_cast<std::uint32_t>(output_attrs_.size());
    for (std::uint32_t output_index = 0; output_index < output_attrs_.size(); ++output_index) {
      const auto element_count = output_attrs_[output_index].n_elems;
      output_buffers_[output_index].resize(element_count);

      outputs_[output_index] = {};
      outputs_[output_index].want_float = 1;
      outputs_[output_index].is_prealloc = 1;
      outputs_[output_index].index = output_index;
      outputs_[output_index].buf = output_buffers_[output_index].data();
      outputs_[output_index].size = static_cast<std::uint32_t>(element_count * sizeof(float));

      if (label_count != 0 && element_count % label_count == 0) {
        const auto candidate_frame_count = element_count / label_count;
        if (candidate_frame_count > 0 &&
            (scores_output_index_ == output_attrs_.size() || candidate_frame_count > score_frame_count_)) {
          scores_output_index_ = output_index;
          score_frame_count_ = static_cast<std::uint32_t>(candidate_frame_count);
        }
      }
    }

    if (scores_output_index_ == output_attrs_.size() || score_frame_count_ == 0) {
      throw std::runtime_error("Failed to identify RKNN YAMNet score output tensor");
    }

    mean_scores_.resize(labels_.size());
  }

  void YamnetModel::load_labels(const std::string& class_map_path) {
    std::ifstream class_map_file{class_map_path};
    if (!class_map_file) {
      throw std::runtime_error("Failed to open YAMNet class map: " + class_map_path);
    }

    std::string line;
    while (std::getline(class_map_file, line)) {
      if (line.empty()) {
        continue;
      }

      std::istringstream line_stream{line};
      std::uint32_t index = 0;
      if (!(line_stream >> index)) {
        throw std::runtime_error("Invalid YAMNet class map line: " + line);
      }

      std::string label;
      std::getline(line_stream, label);
      label = trim_leading_spaces(label);
      if (label.empty()) {
        throw std::runtime_error("Missing YAMNet class label at index " + std::to_string(index));
      }

      if (index >= labels_.size()) {
        labels_.resize(index + 1);
      }
      labels_[index] = std::move(label);
    }

    if (labels_.empty()) {
      throw std::runtime_error("YAMNet class map is empty: " + class_map_path);
    }
  }

  auto YamnetModel::prepare_input(const AudioWindow& audio_window) -> const float* {
    if (audio_window.samples.size() == input_attrs_[0].n_elems) {
      return audio_window.samples.data();
    }

    if (input_samples_.size() != input_attrs_[0].n_elems) {
      input_samples_.resize(input_attrs_[0].n_elems);
    }

    const auto copy_size = std::min(input_samples_.size(), audio_window.samples.size());
    std::copy_n(audio_window.samples.begin(), copy_size, input_samples_.begin());
    if (copy_size < input_samples_.size()) {
      std::fill(input_samples_.begin() + static_cast<std::ptrdiff_t>(copy_size), input_samples_.end(), 0.0F);
    }
    return input_samples_.data();
  }

  auto YamnetModel::post_process_scores(const float* scores) -> std::uint32_t {
    const auto class_count = static_cast<std::uint32_t>(labels_.size());
    for (std::uint32_t class_index = 0; class_index < class_count; ++class_index) {
      float sum = 0.0F;
      for (std::uint32_t frame_index = 0; frame_index < score_frame_count_; ++frame_index) {
        sum += scores[(frame_index * class_count) + class_index];
      }
      mean_scores_[class_index] = sum / static_cast<float>(score_frame_count_);
    }

    for (auto& top_class : top_classes_) {
      top_class.class_index = 0;
      top_class.score = -std::numeric_limits<float>::infinity();
      clear_label(top_class.label);
    }

    for (std::uint32_t class_index = 0; class_index < class_count; ++class_index) {
      const auto score = mean_scores_[class_index];
      for (auto top_index = std::size_t{0}; top_index < top_k_; ++top_index) {
        if (score <= top_classes_[top_index].score) {
          continue;
        }

        for (auto move_index = static_cast<std::size_t>(top_k_ - 1); move_index > top_index; --move_index) {
          top_classes_[move_index] = top_classes_[move_index - 1];
        }

        top_classes_[top_index].class_index = class_index;
        top_classes_[top_index].score = score;
        copy_label(class_label(class_index), top_classes_[top_index].label);
        break;
      }
    }

    return top_k_;
  }

  auto YamnetModel::class_label(std::uint32_t class_index) const -> const std::string& {
    static const std::string kUnknownLabel = "unknown";
    if (class_index >= labels_.size() || labels_[class_index].empty()) {
      return kUnknownLabel;
    }

    return labels_[class_index];
  }

} // namespace signlang::env_sound_det
