#include "handpose_model.hpp"

#include "rknn/include/Float16.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace signlang::handpose_det {
  namespace {

    constexpr auto kBoxChannelCount = std::uint32_t{4};
    constexpr auto kObjectConfidenceChannel = std::uint32_t{4};
    constexpr auto kFirstKeypointChannel = std::uint32_t{5};
    constexpr auto kKeypointFieldCount = std::uint32_t{3};

    auto read_file(const std::string& path) -> std::vector<std::uint8_t> {
      std::ifstream file{path, std::ios::binary | std::ios::ate};
      if (!file) {
        throw std::runtime_error("Failed to open RKNN model: " + path);
      }

      const auto size = file.tellg();
      if (size <= 0 || size > static_cast<std::streamoff>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::runtime_error("Invalid RKNN model file size: " + path);
      }

      std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
      file.seekg(0, std::ios::beg);
      if (!file.read(reinterpret_cast<char*>(data.data()), size)) { // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        throw std::runtime_error("Failed to read RKNN model: " + path);
      }

      return data;
    }

    auto parse_core_mask(const std::string& value) -> rknn_core_mask {
      if (value == "auto") {
        return RKNN_NPU_CORE_AUTO;
      }
      if (value == "0") {
        return RKNN_NPU_CORE_0;
      }
      if (value == "1") {
        return RKNN_NPU_CORE_1;
      }
      if (value == "2") {
        return RKNN_NPU_CORE_2;
      }
      if (value == "0_1") {
        return RKNN_NPU_CORE_0_1;
      }
      if (value == "0_1_2") {
        return RKNN_NPU_CORE_0_1_2;
      }
      if (value == "all") {
        return RKNN_NPU_CORE_ALL;
      }

      throw std::runtime_error("Unsupported --npu-core value: " + value);
    }

    auto checked_ret(int ret, const char* operation) -> void {
      if (ret != RKNN_SUCC) {
        throw std::runtime_error(std::string(operation) + " failed, ret=" + std::to_string(ret));
      }
    }

    auto tensor_dim(const rknn_tensor_attr& attr, std::uint32_t index) -> std::uint32_t {
      if (index >= attr.n_dims) {
        throw std::runtime_error("Unexpected RKNN tensor rank");
      }
      return attr.dims[index];
    }

    auto input_width(const rknn_tensor_attr& attr) -> std::uint32_t {
      return attr.fmt == RKNN_TENSOR_NCHW ? tensor_dim(attr, 3) : tensor_dim(attr, 2);
    }

    auto input_height(const rknn_tensor_attr& attr) -> std::uint32_t {
      return attr.fmt == RKNN_TENSOR_NCHW ? tensor_dim(attr, 2) : tensor_dim(attr, 1);
    }

    auto input_channels(const rknn_tensor_attr& attr) -> std::uint32_t {
      return attr.fmt == RKNN_TENSOR_NCHW ? tensor_dim(attr, 1) : tensor_dim(attr, 3);
    }

    auto output_channels(const rknn_tensor_attr& attr) -> std::uint32_t {
      if (attr.n_dims != 3 && attr.n_dims != 4) {
        throw std::runtime_error("YOLOv8 handpose model output must have rank 3 or 4");
      }
      return attr.n_dims == 3 ? tensor_dim(attr, 1) : tensor_dim(attr, 1);
    }

    auto output_candidates(const rknn_tensor_attr& attr) -> std::uint32_t {
      if (attr.n_dims == 3) {
        return tensor_dim(attr, 2);
      }
      return tensor_dim(attr, 2) * tensor_dim(attr, 3);
    }

    auto box_iou(const HandPoseBox& lhs, const HandPoseBox& rhs) -> float {
      const auto inter_left = std::max(lhs.left, rhs.left);
      const auto inter_top = std::max(lhs.top, rhs.top);
      const auto inter_right = std::min(lhs.right, rhs.right);
      const auto inter_bottom = std::min(lhs.bottom, rhs.bottom);
      const auto inter_width = std::max(0.0F, inter_right - inter_left);
      const auto inter_height = std::max(0.0F, inter_bottom - inter_top);
      const auto inter_area = inter_width * inter_height;
      const auto lhs_area = std::max(0.0F, lhs.right - lhs.left) * std::max(0.0F, lhs.bottom - lhs.top);
      const auto rhs_area = std::max(0.0F, rhs.right - rhs.left) * std::max(0.0F, rhs.bottom - rhs.top);
      const auto union_area = lhs_area + rhs_area - inter_area;
      return union_area <= 0.0F ? 0.0F : inter_area / union_area;
    }

    auto scale_to_image(float value, std::uint32_t pad, float scale, std::uint32_t max_value) -> float {
      if (scale <= 0.0F) {
        return 0.0F;
      }

      return std::clamp((value - static_cast<float>(pad)) / scale, 0.0F, static_cast<float>(max_value));
    }

  } // namespace

  HandPoseModel::HandPoseModel(std::string model_path, std::string runtime_library_path,
                               const ProgramOptions& options) :
      model_path_{std::move(model_path)}, runtime_{std::move(runtime_library_path)}, context_{0}, io_num_{},
      input_mem_{nullptr}, output_mem_{nullptr}, preprocessor_{1, 1}, confidence_threshold_{options.confidence_threshold},
      nms_threshold_{options.nms_threshold}, keypoint_count_{options.keypoint_count},
      max_detections_{options.max_detections}, model_width_{0}, model_height_{0}, output_channel_count_{0},
      output_candidate_count_{0}, output_stride_candidate_count_{0} {
    load_model(model_path_, options);
    configure_io(options);
  }

  HandPoseModel::~HandPoseModel() {
    if (output_mem_ != nullptr) {
      runtime_.destroy_mem(context_, output_mem_);
      output_mem_ = nullptr;
    }
    if (input_mem_ != nullptr) {
      runtime_.destroy_mem(context_, input_mem_);
      input_mem_ = nullptr;
    }
    if (context_ != 0) {
      runtime_.destroy(context_);
      context_ = 0;
    }
  }

  auto HandPoseModel::run(const signlang::video_frontend::VideoFrameMetadata& metadata, const std::uint8_t* payload,
                          std::uint64_t payload_size,
                          iox2::bb::MutableSlice<HandPoseDetection> detections) -> InferenceResult {
    preprocessor_.process(metadata, payload, payload_size, static_cast<std::uint8_t*>(input_mem_->virt_addr),
                          input_stride_width_pixels());

    checked_ret(runtime_.mem_sync(context_, input_mem_, RKNN_MEMORY_SYNC_TO_DEVICE), "rknn_mem_sync(input)");
    checked_ret(runtime_.run(context_, nullptr), "rknn_run");
    checked_ret(runtime_.mem_sync(context_, output_mem_, RKNN_MEMORY_SYNC_FROM_DEVICE), "rknn_mem_sync(output)");

    const auto detection_count = parse_output(preprocessor_.letterbox(), metadata, detections);
    return InferenceResult{
        .detection_count = detection_count,
        .image_width = metadata.output_width,
        .image_height = metadata.output_height,
        .model_width = model_width_,
        .model_height = model_height_,
    };
  }

  void HandPoseModel::load_model(const std::string& model_path, const ProgramOptions& options) {
    auto model_data = read_file(model_path);
    const auto flags = RKNN_FLAG_PRIOR_LOW;
    checked_ret(runtime_.init(&context_, model_data.data(), static_cast<std::uint32_t>(model_data.size()), flags, nullptr),
                "rknn_init");

    checked_ret(runtime_.query(context_, RKNN_QUERY_IN_OUT_NUM, &io_num_, sizeof(io_num_)),
                "rknn_query(IN_OUT_NUM)");
    if (io_num_.n_input != 1 || io_num_.n_output != 1) {
      throw std::runtime_error("Expected one RKNN input and one RKNN output for yolov8 handpose model");
    }

    input_attrs_.resize(io_num_.n_input);
    for (std::uint32_t i = 0; i < io_num_.n_input; ++i) {
      auto& attr = input_attrs_[i];
      std::memset(&attr, 0, sizeof(attr));
      attr.index = i;
      checked_ret(runtime_.query(context_, RKNN_QUERY_INPUT_ATTR, &attr, sizeof(attr)), "rknn_query(INPUT_ATTR)");
    }

    output_attrs_.resize(io_num_.n_output);
    for (std::uint32_t i = 0; i < io_num_.n_output; ++i) {
      auto& attr = output_attrs_[i];
      std::memset(&attr, 0, sizeof(attr));
      attr.index = i;
      checked_ret(runtime_.query(context_, RKNN_QUERY_OUTPUT_ATTR, &attr, sizeof(attr)), "rknn_query(OUTPUT_ATTR)");
    }

    checked_ret(runtime_.set_core_mask(context_, parse_core_mask(options.npu_core_mask)), "rknn_set_core_mask");

    model_width_ = input_width(input_attrs_[0]);
    model_height_ = input_height(input_attrs_[0]);
    if (input_channels(input_attrs_[0]) != 3) {
      throw std::runtime_error("YOLOv8 handpose model input must have 3 channels");
    }

    output_channel_count_ = output_channels(output_attrs_[0]);
    output_candidate_count_ = output_candidates(output_attrs_[0]);
    output_stride_candidate_count_ = output_stride_candidate_count();
    const auto expected_channels = kFirstKeypointChannel + (keypoint_count_ * kKeypointFieldCount);
    if (output_channel_count_ != expected_channels) {
      throw std::runtime_error("Unexpected YOLOv8 handpose output channels: got " +
                               std::to_string(output_channel_count_) + ", expected " +
                               std::to_string(expected_channels));
    }

    if (options.verbose) {
      print_tensor_details();
    }

    preprocessor_ = HandPosePreprocessor{model_width_, model_height_};
  }

  void HandPoseModel::configure_io(const ProgramOptions& /* options */) {
    auto input_attr = input_attrs_[0];
    input_attr.type = RKNN_TENSOR_UINT8;
    input_attr.fmt = RKNN_TENSOR_NHWC;
    input_attr.pass_through = 0;
    input_mem_ = runtime_.create_mem(context_, input_attr.size_with_stride == 0 ? input_attr.size : input_attr.size_with_stride);
    if (input_mem_ == nullptr) {
      throw std::runtime_error("rknn_create_mem(input) failed");
    }
    checked_ret(runtime_.set_io_mem(context_, input_mem_, &input_attr), "rknn_set_io_mem(input)");
    input_attrs_[0] = input_attr;

    auto output_attr = output_attrs_[0];
    output_attr.type = output_attr.type == RKNN_TENSOR_FLOAT16 ? RKNN_TENSOR_FLOAT16 : output_attr.type;
    output_attr.fmt = RKNN_TENSOR_NCHW;
    const auto output_size = output_attr.size_with_stride == 0 ? output_attr.size : output_attr.size_with_stride;
    output_mem_ = runtime_.create_mem(context_, output_size);
    if (output_mem_ == nullptr) {
      throw std::runtime_error("rknn_create_mem(output) failed");
    }
    checked_ret(runtime_.set_io_mem(context_, output_mem_, &output_attr), "rknn_set_io_mem(output)");
    output_attrs_[0] = output_attr;

    candidates_.reserve(output_candidate_count_);
    order_.reserve(output_candidate_count_);
  }

  void HandPoseModel::print_tensor_details() const {
    const auto& input = input_attrs_[0];
    const auto& output = output_attrs_[0];
    std::cout << "RKNN input: name=" << input.name << " dims=[";
    for (std::uint32_t i = 0; i < input.n_dims; ++i) {
      std::cout << (i == 0 ? "" : ",") << input.dims[i];
    }
    std::cout << "] size=" << input.size << " stride_size=" << input.size_with_stride << '\n';

    std::cout << "RKNN output: name=" << output.name << " dims=[";
    for (std::uint32_t i = 0; i < output.n_dims; ++i) {
      std::cout << (i == 0 ? "" : ",") << output.dims[i];
    }
    std::cout << "] size=" << output.size << " stride_size=" << output.size_with_stride << '\n';
  }

  auto HandPoseModel::input_stride_width_pixels() const -> std::uint32_t {
    return input_attrs_[0].w_stride == 0 ? model_width_ : input_attrs_[0].w_stride;
  }

  auto HandPoseModel::output_stride_candidate_count() const -> std::uint32_t {
    const auto& attr = output_attrs_[0];
    if (attr.n_dims == 3) {
      return attr.w_stride == 0 ? tensor_dim(attr, 2) : attr.w_stride;
    }

    const auto width_stride = attr.w_stride == 0 ? tensor_dim(attr, 3) : attr.w_stride;
    return tensor_dim(attr, 2) * width_stride;
  }

  auto HandPoseModel::output_value(std::uint32_t channel, std::uint32_t candidate_index) const -> float {
    const auto offset = static_cast<std::uint64_t>(channel) * output_stride_candidate_count_ + candidate_index;
    if (output_attrs_[0].type == RKNN_TENSOR_FLOAT16) {
      const auto* data = static_cast<const rknpu2::float16*>(output_mem_->virt_addr);
      return static_cast<float>(data[offset]);
    }
    if (output_attrs_[0].type == RKNN_TENSOR_FLOAT32) {
      const auto* data = static_cast<const float*>(output_mem_->virt_addr);
      return data[offset];
    }
    if (output_attrs_[0].type == RKNN_TENSOR_INT8) {
      const auto* data = static_cast<const std::int8_t*>(output_mem_->virt_addr);
      return (static_cast<float>(data[offset]) - static_cast<float>(output_attrs_[0].zp)) * output_attrs_[0].scale;
    }
    if (output_attrs_[0].type == RKNN_TENSOR_UINT8) {
      const auto* data = static_cast<const std::uint8_t*>(output_mem_->virt_addr);
      return (static_cast<float>(data[offset]) - static_cast<float>(output_attrs_[0].zp)) * output_attrs_[0].scale;
    }

    throw std::runtime_error("Unsupported RKNN output tensor type");
  }

  auto HandPoseModel::parse_output(const LetterboxInfo& letterbox,
                                   const signlang::video_frontend::VideoFrameMetadata& metadata,
                                   iox2::bb::MutableSlice<HandPoseDetection> detections) -> std::uint32_t {
    candidates_.clear();
    order_.clear();

    for (std::uint32_t index = 0; index < output_candidate_count_; ++index) {
      const auto confidence = output_value(kObjectConfidenceChannel, index);
      if (confidence < confidence_threshold_) {
        continue;
      }

      const auto cx = output_value(0, index);
      const auto cy = output_value(1, index);
      const auto width = output_value(2, index);
      const auto height = output_value(3, index);

      auto detection = HandPoseDetection{};
      detection.box = HandPoseBox{
          .left = scale_to_image(cx - width / 2.0F, letterbox.x_pad, letterbox.scale, metadata.output_width),
          .top = scale_to_image(cy - height / 2.0F, letterbox.y_pad, letterbox.scale, metadata.output_height),
          .right = scale_to_image(cx + width / 2.0F, letterbox.x_pad, letterbox.scale, metadata.output_width),
          .bottom = scale_to_image(cy + height / 2.0F, letterbox.y_pad, letterbox.scale, metadata.output_height),
      };
      detection.confidence = confidence;
      detection.class_id = 0;

      for (std::uint32_t keypoint = 0; keypoint < keypoint_count_; ++keypoint) {
        const auto base = kFirstKeypointChannel + keypoint * kKeypointFieldCount;
        detection.keypoints[keypoint] = HandPoseKeypoint{
            .x = scale_to_image(output_value(base, index), letterbox.x_pad, letterbox.scale, metadata.output_width),
            .y = scale_to_image(output_value(base + 1, index), letterbox.y_pad, letterbox.scale, metadata.output_height),
            .confidence = output_value(base + 2, index),
        };
      }

      candidates_.push_back(Candidate{.detection = detection, .index = index, .suppressed = false});
    }

    std::sort(candidates_.begin(), candidates_.end(), [](const Candidate& lhs, const Candidate& rhs) {
      return lhs.detection.confidence > rhs.detection.confidence;
    });

    std::uint32_t published_count = 0;
    const auto publish_limit = std::min<std::uint64_t>(detections.number_of_elements(), max_detections_);
    for (std::uint32_t i = 0; i < candidates_.size() && published_count < publish_limit; ++i) {
      if (candidates_[i].suppressed) {
        continue;
      }

      detections[published_count++] = candidates_[i].detection;

      for (std::uint32_t j = i + 1; j < candidates_.size(); ++j) {
        if (!candidates_[j].suppressed &&
            box_iou(candidates_[i].detection.box, candidates_[j].detection.box) > nms_threshold_) {
          candidates_[j].suppressed = true;
        }
      }
    }

    return published_count;
  }

} // namespace signlang::handpose_det
