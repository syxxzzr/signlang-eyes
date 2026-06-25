#include "handpose_model.hpp"

#include "Float16.h"
#include "im2d.h"
#include "rga.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace signlang::handpose_det {
  namespace {

    constexpr auto kRgbChannelCount = std::uint32_t{3};
    constexpr auto kPalmInputSize = std::uint32_t{192};
    constexpr auto kLandmarkInputSize = std::uint32_t{224};
    constexpr auto kPalmBoxValueCount = std::uint32_t{18};
    constexpr auto kPalmKeypointValueCount = std::uint32_t{14};
    constexpr auto kPalmAnchorCount = std::uint32_t{2016};
    constexpr auto kPalmScoreClip = 100.0F;
    constexpr auto kPalmBoxScale = 192.0F;
    constexpr auto kLandmarkValueCount = std::uint32_t{63};
    constexpr auto kPi = 3.14159265358979F;

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
      if (!file.read(reinterpret_cast<char*>(data.data()),
                     size)) { // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        throw std::runtime_error("Failed to read RKNN model: " + path);
      }

      return data;
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

    auto tensor_width(const rknn_tensor_attr& attr) -> std::uint32_t {
      return attr.fmt == RKNN_TENSOR_NCHW ? tensor_dim(attr, 3) : tensor_dim(attr, 2);
    }

    auto tensor_height(const rknn_tensor_attr& attr) -> std::uint32_t {
      return attr.fmt == RKNN_TENSOR_NCHW ? tensor_dim(attr, 2) : tensor_dim(attr, 1);
    }

    auto tensor_channels(const rknn_tensor_attr& attr) -> std::uint32_t {
      return attr.fmt == RKNN_TENSOR_NCHW ? tensor_dim(attr, 1) : tensor_dim(attr, 3);
    }

    auto dims_string(const rknn_tensor_attr& attr) -> std::string {
      auto result = std::string{};
      for (std::uint32_t i = 0; i < attr.n_dims; ++i) {
        if (i != 0) {
          result += ',';
        }
        result += std::to_string(attr.dims[i]);
      }
      return result;
    }

    auto sigmoid(float value) -> float {
      if (value < -kPalmScoreClip) {
        return 0.0F;
      }
      if (value > kPalmScoreClip) {
        return 1.0F;
      }
      return 1.0F / (1.0F + std::exp(-value));
    }

    auto checked_rgb_size_bytes(std::uint32_t width, std::uint32_t height) -> std::uint64_t {
      return static_cast<std::uint64_t>(width) * height * kRgbChannelCount;
    }

    auto output_element_count(const rknn_tensor_attr& attr) -> std::uint64_t {
      auto count = std::uint64_t{1};
      for (std::uint32_t i = 0; i < attr.n_dims; ++i) {
        count *= attr.dims[i];
      }
      return count;
    }

    auto tensor_value(const rknn_tensor_attr& attr, const rknn_tensor_mem* mem, std::uint64_t offset) -> float {
      if (attr.type == RKNN_TENSOR_FLOAT16) {
        const auto* data = static_cast<const rknpu2::float16*>(mem->virt_addr);
        return static_cast<float>(data[offset]);
      }
      if (attr.type == RKNN_TENSOR_FLOAT32) {
        const auto* data = static_cast<const float*>(mem->virt_addr);
        return data[offset];
      }
      if (attr.type == RKNN_TENSOR_INT8) {
        const auto* data = static_cast<const std::int8_t*>(mem->virt_addr);
        return (static_cast<float>(data[offset]) - static_cast<float>(attr.zp)) * attr.scale;
      }
      if (attr.type == RKNN_TENSOR_UINT8) {
        const auto* data = static_cast<const std::uint8_t*>(mem->virt_addr);
        return (static_cast<float>(data[offset]) - static_cast<float>(attr.zp)) * attr.scale;
      }
      throw std::runtime_error("Unsupported RKNN tensor type");
    }

    void resize_rgb_with_rga(const std::uint8_t* src_data, std::uint32_t src_width, std::uint32_t src_height,
                             std::uint8_t* dst_data, std::uint32_t dst_width, std::uint32_t dst_height,
                             im_rect src_rect) {
      const auto src_size = checked_rgb_size_bytes(src_width, src_height);
      const auto dst_size = checked_rgb_size_bytes(dst_width, dst_height);
      const auto src_handle = importbuffer_virtualaddr(const_cast<std::uint8_t*>(src_data), static_cast<int>(src_size));
      if (src_handle == 0) {
        throw std::runtime_error("RGA: failed to import handpose source buffer");
      }

      const auto dst_handle = importbuffer_virtualaddr(dst_data, static_cast<int>(dst_size));
      if (dst_handle == 0) {
        releasebuffer_handle(src_handle);
        throw std::runtime_error("RGA: failed to import handpose destination buffer");
      }

      auto src_img =
          wrapbuffer_handle(src_handle, static_cast<int>(src_width), static_cast<int>(src_height), RK_FORMAT_RGB_888);
      auto dst_img =
          wrapbuffer_handle(dst_handle, static_cast<int>(dst_width), static_cast<int>(dst_height), RK_FORMAT_RGB_888);
      const auto dst_rect =
          im_rect{.x = 0, .y = 0, .width = static_cast<int>(dst_width), .height = static_cast<int>(dst_height)};
      const auto empty_rect = im_rect{.x = 0, .y = 0, .width = 0, .height = 0};
      const auto status = improcess(src_img, dst_img, {}, src_rect, dst_rect, empty_rect, IM_SYNC);

      releasebuffer_handle(dst_handle);
      releasebuffer_handle(src_handle);

      if (status != IM_STATUS_SUCCESS) {
        throw std::runtime_error(std::string{"RGA handpose resize failed with status: "} +
                                 std::to_string(static_cast<int>(status)));
      }
    }

  } // namespace

  struct HandPoseModel::Anchor {
    float x_center;
    float y_center;
  };

  struct HandPoseModel::RknnModel {
    explicit RknnModel(const std::string& model_path, rknn_core_mask core_mask) :
        context{0}, io_num{}, input_mem{nullptr}, input_width{0}, input_height{0}, input_channels{0} {
      auto model_data = read_file(model_path);
      checked_ret(rknn_init(&context, model_data.data(), static_cast<std::uint32_t>(model_data.size()),
                            RKNN_FLAG_PRIOR_LOW, nullptr),
                  "rknn_init");
      checked_ret(rknn_set_core_mask(context, core_mask), "rknn_set_core_mask");
      checked_ret(rknn_query(context, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num)), "rknn_query(IN_OUT_NUM)");
      if (io_num.n_input != 1 || io_num.n_output == 0) {
        throw std::runtime_error("MediaPipe RKNN model must have one input and at least one output");
      }

      input_attrs.resize(io_num.n_input);
      for (std::uint32_t i = 0; i < io_num.n_input; ++i) {
        auto& attr = input_attrs[i];
        std::memset(&attr, 0, sizeof(attr));
        attr.index = i;
        checked_ret(rknn_query(context, RKNN_QUERY_INPUT_ATTR, &attr, sizeof(attr)), "rknn_query(INPUT_ATTR)");
      }

      output_attrs.resize(io_num.n_output);
      for (std::uint32_t i = 0; i < io_num.n_output; ++i) {
        auto& attr = output_attrs[i];
        std::memset(&attr, 0, sizeof(attr));
        attr.index = i;
        checked_ret(rknn_query(context, RKNN_QUERY_OUTPUT_ATTR, &attr, sizeof(attr)), "rknn_query(OUTPUT_ATTR)");
      }

      input_width = tensor_width(input_attrs[0]);
      input_height = tensor_height(input_attrs[0]);
      input_channels = tensor_channels(input_attrs[0]);

      auto input_attr = input_attrs[0];
      input_attr.type = RKNN_TENSOR_UINT8;
      input_attr.fmt = RKNN_TENSOR_NHWC;
      input_attr.pass_through = 0;
      input_mem =
          rknn_create_mem(context, input_attr.size_with_stride == 0 ? input_attr.size : input_attr.size_with_stride);
      if (input_mem == nullptr) {
        throw std::runtime_error("rknn_create_mem(input) failed");
      }
      checked_ret(rknn_set_io_mem(context, input_mem, &input_attr), "rknn_set_io_mem(input)");
      input_attrs[0] = input_attr;

      output_mems.resize(output_attrs.size(), nullptr);
      for (std::uint32_t i = 0; i < output_attrs.size(); ++i) {
        auto output_attr = output_attrs[i];
        output_attr.fmt = RKNN_TENSOR_NCHW;
        output_mems[i] = rknn_create_mem(
            context, output_attr.size_with_stride == 0 ? output_attr.size : output_attr.size_with_stride);
        if (output_mems[i] == nullptr) {
          throw std::runtime_error("rknn_create_mem(output) failed");
        }
        checked_ret(rknn_set_io_mem(context, output_mems[i], &output_attr), "rknn_set_io_mem(output)");
        output_attrs[i] = output_attr;
      }
    }

    ~RknnModel() {
      for (auto* mem : output_mems) {
        if (mem != nullptr) {
          rknn_destroy_mem(context, mem);
        }
      }
      if (input_mem != nullptr) {
        rknn_destroy_mem(context, input_mem);
      }
      if (context != 0) {
        rknn_destroy(context);
      }
    }

    RknnModel(const RknnModel&) = delete;
    auto operator=(const RknnModel&) -> RknnModel& = delete;

    auto width() const -> std::uint32_t { return input_width; }
    auto height() const -> std::uint32_t { return input_height; }

    auto input_stride_width_pixels() const -> std::uint32_t {
      return input_attrs[0].w_stride == 0 ? width() : input_attrs[0].w_stride;
    }

    auto input_data() -> std::uint8_t* { return static_cast<std::uint8_t*>(input_mem->virt_addr); }

    void run() {
      checked_ret(rknn_mem_sync(context, input_mem, RKNN_MEMORY_SYNC_TO_DEVICE), "rknn_mem_sync(input)");
      checked_ret(rknn_run(context, nullptr), "rknn_run");
      for (auto* mem : output_mems) {
        checked_ret(rknn_mem_sync(context, mem, RKNN_MEMORY_SYNC_FROM_DEVICE), "rknn_mem_sync(output)");
      }
    }

    auto output_value(std::uint32_t output_index, std::uint64_t offset) const -> float {
      return tensor_value(output_attrs[output_index], output_mems[output_index], offset);
    }

    rknn_context context;
    rknn_input_output_num io_num;
    std::vector<rknn_tensor_attr> input_attrs;
    std::vector<rknn_tensor_attr> output_attrs;
    rknn_tensor_mem* input_mem;
    std::vector<rknn_tensor_mem*> output_mems;
    std::uint32_t input_width;
    std::uint32_t input_height;
    std::uint32_t input_channels;
  };

  HandPoseModel::HandPoseModel(std::string palm_detector_model_path, std::string landmark_model_path,
                               const ProgramOptions& options) :
      palm_detector_model_path_{std::move(palm_detector_model_path)},
      landmark_model_path_{std::move(landmark_model_path)}, palm_detector_{nullptr}, landmark_model_{nullptr},
      current_frame_number_{0}, confidence_threshold_{options.confidence_threshold},
      presence_threshold_{options.presence_threshold}, tracking_threshold_{options.tracking_threshold},
      nms_iou_threshold_{options.nms_iou_threshold},
      tracking_iou_match_threshold_{options.tracking_iou_match_threshold}, crop_expansion_{options.crop_expansion},
      base_roi_expansion_{options.base_roi_expansion}, small_hand_expansion_{options.small_hand_expansion},
      large_hand_expansion_{options.large_hand_expansion}, small_hand_threshold_{options.small_hand_threshold},
      large_hand_threshold_{options.large_hand_threshold}, boundary_margin_{options.boundary_margin},
      boundary_min_factor_{options.boundary_min_factor}, euro_min_cutoff_{options.euro_min_cutoff},
      euro_beta_{options.euro_beta}, euro_d_cutoff_{options.euro_d_cutoff},
      handedness_threshold_{options.handedness_threshold}, max_tracking_gap_{options.max_tracking_gap},
      max_stale_frames_{options.max_stale_frames}, keypoint_count_{options.keypoint_count},
      output_hands_{options.output_hands}, model_width_{kPalmInputSize}, model_height_{kPalmInputSize} {
    if (keypoint_count_ != kHandPoseKeypointCount) {
      throw std::runtime_error("MediaPipe hand landmark model requires exactly 21 keypoints");
    }
    initialize_models(options);
    validate_models();
    build_palm_anchors();
    candidates_.reserve(kPalmAnchorCount);
    selected_.reserve(output_hands_);
    tracked_hands_.reserve(output_hands_);
    keypoint_filters_.resize(output_hands_);
    for (auto& hand_filters : keypoint_filters_) {
      hand_filters.resize(kHandPoseKeypointCount * 3);
      for (auto& filter : hand_filters) {
        filter = OneEuroFilter{.x = 0.0F,
                               .dx = 0.0F,
                               .min_cutoff = euro_min_cutoff_,
                               .beta = euro_beta_,
                               .d_cutoff = euro_d_cutoff_,
                               .last_time = 0,
                               .initialized = false};
      }
    }
  }

  HandPoseModel::~HandPoseModel() = default;

  auto HandPoseModel::run(const signlang::video_frontend::VideoFrameMetadata& metadata, const std::uint8_t* payload,
                          std::uint64_t payload_size, iox2::bb::MutableSlice<HandPoseDetection> detections)
      -> InferenceResult {
    if (detections.number_of_elements() < output_hands_) {
      throw std::runtime_error("Hand pose output slice is smaller than requested hand count");
    }

    for (std::uint32_t i = 0; i < output_hands_; ++i) {
      detections[i] = HandPoseDetection{};
      detections[i].present = false;
    }

    current_frame_number_++;

    try_tracking_from_previous_frame(metadata, payload, payload_size);

    auto tracked_count = std::uint32_t{0};
    for (const auto& tracked : tracked_hands_) {
      if (tracked.last_seen_frame == current_frame_number_) {
        tracked_count++;
      }
    }

    if (tracked_count < output_hands_) {
      run_palm_detector(metadata, payload, payload_size);
      apply_nms();

      std::sort(candidates_.begin(), candidates_.end(), [](const PalmCandidate& lhs, const PalmCandidate& rhs) {
        return lhs.detection.confidence > rhs.detection.confidence;
      });

      selected_.clear();
      for (std::size_t i = 0; i < candidates_.size() && selected_.size() < output_hands_ - tracked_count; ++i) {
        auto already_tracked = false;
        for (const auto& tracked : tracked_hands_) {
          if (tracked.last_seen_frame == current_frame_number_ &&
              compute_iou(tracked.roi, candidates_[i].detection.box) > tracking_iou_match_threshold_) {
            already_tracked = true;
            break;
          }
        }
        if (!already_tracked) {
          selected_.push_back(candidates_[i]);
        }
      }

      std::sort(selected_.begin(), selected_.end(), [](const PalmCandidate& lhs, const PalmCandidate& rhs) {
        const auto lhs_center = (lhs.detection.box.left + lhs.detection.box.right) * 0.5F;
        const auto rhs_center = (rhs.detection.box.left + rhs.detection.box.right) * 0.5F;
        return lhs_center < rhs_center;
      });

      for (auto& selected : selected_) {
        run_landmark_detector(metadata, payload, payload_size, selected);
      }
    }

    update_tracked_hands(selected_);

    for (std::size_t i = 0; i < tracked_hands_.size() && static_cast<std::uint32_t>(i) < output_hands_; ++i) {
      if (tracked_hands_[i].last_seen_frame == current_frame_number_) {
        smooth_keypoints_hand(i, metadata.timestamp_ns);
      }
    }

    auto output_idx = std::uint32_t{0};
    for (const auto& tracked : tracked_hands_) {
      if (tracked.last_seen_frame == current_frame_number_ && output_idx < output_hands_) {
        detections[output_idx] = HandPoseDetection{
            .box = tracked.roi,
            .keypoints = tracked.smoothed_keypoints,
            .confidence = tracked.palm_confidence,
            .presence_confidence = tracked.presence_confidence,
            .class_id = 0,
            .present = true,
            .is_left_hand = tracked.is_left_hand,
        };
        output_idx++;
      }
    }

    return InferenceResult{
        .detection_count = output_hands_,
        .image_width = metadata.output_width,
        .image_height = metadata.output_height,
        .model_width = palm_detector_->width(),
        .model_height = palm_detector_->height(),
    };
  }

  void HandPoseModel::initialize_models(const ProgramOptions& options) {
    palm_detector_ = std::make_unique<RknnModel>(palm_detector_model_path_, options.palm_detector_npu_core_mask);
    landmark_model_ = std::make_unique<RknnModel>(landmark_model_path_, options.landmark_npu_core_mask);
    if (options.verbose) {
      print_tensor_details();
    }
  }

  void HandPoseModel::validate_models() const {
    if (palm_detector_->width() != kPalmInputSize || palm_detector_->height() != kPalmInputSize ||
        palm_detector_->input_channels != kRgbChannelCount) {
      throw std::runtime_error("MediaPipe palm detector input must be RGB 192x192");
    }
    if (landmark_model_->width() != kLandmarkInputSize || landmark_model_->height() != kLandmarkInputSize ||
        landmark_model_->input_channels != kRgbChannelCount) {
      throw std::runtime_error("MediaPipe hand landmark input must be RGB 224x224");
    }
    if (palm_detector_->output_attrs.size() != 2 ||
        output_element_count(palm_detector_->output_attrs[0]) !=
            static_cast<std::uint64_t>(kPalmAnchorCount) * kPalmBoxValueCount ||
        output_element_count(palm_detector_->output_attrs[1]) != kPalmAnchorCount) {
      throw std::runtime_error("Unexpected MediaPipe palm detector output shape");
    }
    if (landmark_model_->output_attrs.size() < 3 ||
        output_element_count(landmark_model_->output_attrs[0]) != kLandmarkValueCount ||
        output_element_count(landmark_model_->output_attrs[1]) != 1) {
      throw std::runtime_error("Unexpected MediaPipe hand landmark output shape");
    }
  }

  void HandPoseModel::print_tensor_details() const {
    const auto log_model = [](const char* label, const RknnModel& model) {
      spdlog::info("{} input: name={} dims=[{}] size={} stride_size={}", label, model.input_attrs[0].name,
                   dims_string(model.input_attrs[0]), model.input_attrs[0].size, model.input_attrs[0].size_with_stride);
      for (std::uint32_t i = 0; i < model.output_attrs.size(); ++i) {
        const auto& output = model.output_attrs[i];
        spdlog::info("{} output[{}]: name={} dims=[{}] size={} stride_size={}", label, i, output.name,
                     dims_string(output), output.size, output.size_with_stride);
      }
    };
    log_model("Palm detector", *palm_detector_);
    log_model("Landmark", *landmark_model_);
  }

  void HandPoseModel::build_palm_anchors() {
    anchors_.clear();
    anchors_.reserve(kPalmAnchorCount);
    constexpr auto strides = std::array<std::uint32_t, 4>{8, 16, 16, 16};

    for (const auto stride : strides) {
      const auto feature_map_size =
          static_cast<std::uint32_t>(std::ceil(static_cast<float>(kPalmInputSize) / static_cast<float>(stride)));
      for (std::uint32_t y = 0; y < feature_map_size; ++y) {
        for (std::uint32_t x = 0; x < feature_map_size; ++x) {
          const auto x_center = (static_cast<float>(x) + 0.5F) / static_cast<float>(feature_map_size);
          const auto y_center = (static_cast<float>(y) + 0.5F) / static_cast<float>(feature_map_size);
          anchors_.push_back(Anchor{.x_center = x_center, .y_center = y_center});
          anchors_.push_back(Anchor{.x_center = x_center, .y_center = y_center});
        }
      }
    }

    if (anchors_.size() != kPalmAnchorCount) {
      throw std::runtime_error("Internal MediaPipe palm anchor count mismatch");
    }
  }

  void HandPoseModel::run_palm_detector(const signlang::video_frontend::VideoFrameMetadata& metadata,
                                        const std::uint8_t* payload, std::uint64_t payload_size) {
    if (metadata.pixel_format != signlang::video_frontend::kPixelFormatRgb24) {
      throw std::runtime_error("Hand pose detector supports RGB24 video input only");
    }
    if (metadata.output_width == 0 || metadata.output_height == 0) {
      throw std::runtime_error("Invalid upstream video frame dimensions");
    }
    if (payload_size < checked_rgb_size_bytes(metadata.output_width, metadata.output_height)) {
      throw std::runtime_error("Upstream RGB video frame payload is smaller than metadata dimensions");
    }

    const auto full_frame = im_rect{.x = 0,
                                    .y = 0,
                                    .width = static_cast<int>(metadata.output_width),
                                    .height = static_cast<int>(metadata.output_height)};
    resize_rgb_with_rga(payload, metadata.output_width, metadata.output_height, palm_detector_->input_data(),
                        palm_detector_->width(), palm_detector_->height(), full_frame);
    palm_detector_->run();

    candidates_.clear();
    for (std::uint32_t i = 0; i < kPalmAnchorCount; ++i) {
      const auto score = sigmoid(palm_detector_->output_value(1, i));
      if (score < confidence_threshold_) {
        continue;
      }

      const auto box_offset = static_cast<std::uint64_t>(i) * kPalmBoxValueCount;
      const auto& anchor = anchors_[i];
      const auto cx_norm = (palm_detector_->output_value(0, box_offset) / kPalmBoxScale) + anchor.x_center;
      const auto cy_norm = (palm_detector_->output_value(0, box_offset + 1) / kPalmBoxScale) + anchor.y_center;
      const auto w_norm = palm_detector_->output_value(0, box_offset + 2) / kPalmBoxScale;
      const auto h_norm = palm_detector_->output_value(0, box_offset + 3) / kPalmBoxScale;

      auto candidate = PalmCandidate{};
      candidate.detection.box = HandPoseBox{
          .left = std::clamp((cx_norm - w_norm * 0.5F) * static_cast<float>(metadata.output_width), 0.0F,
                             static_cast<float>(metadata.output_width)),
          .top = std::clamp((cy_norm - h_norm * 0.5F) * static_cast<float>(metadata.output_height), 0.0F,
                            static_cast<float>(metadata.output_height)),
          .right = std::clamp((cx_norm + w_norm * 0.5F) * static_cast<float>(metadata.output_width), 0.0F,
                              static_cast<float>(metadata.output_width)),
          .bottom = std::clamp((cy_norm + h_norm * 0.5F) * static_cast<float>(metadata.output_height), 0.0F,
                               static_cast<float>(metadata.output_height)),
      };
      candidate.detection.confidence = score;
      candidate.detection.class_id = 0;

      for (std::uint32_t k = 0; k < kPalmKeypointValueCount; k += 2) {
        candidate.palm_keypoints[k] =
            ((palm_detector_->output_value(0, box_offset + 4 + k) / kPalmBoxScale) + anchor.x_center) *
            static_cast<float>(metadata.output_width);
        candidate.palm_keypoints[k + 1] =
            ((palm_detector_->output_value(0, box_offset + 5 + k) / kPalmBoxScale) + anchor.y_center) *
            static_cast<float>(metadata.output_height);
      }

      if (candidate.detection.box.right > candidate.detection.box.left &&
          candidate.detection.box.bottom > candidate.detection.box.top) {
        candidates_.push_back(candidate);
      }
    }
  }

  auto HandPoseModel::crop_transform_from_box(const HandPoseBox& box, std::uint32_t image_width,
                                              std::uint32_t image_height) const -> CropTransform {
    const auto center_x = (box.left + box.right) * 0.5F;
    const auto center_y = (box.top + box.bottom) * 0.5F;
    const auto box_width = std::max(1.0F, box.right - box.left);
    const auto box_height = std::max(1.0F, box.bottom - box.top);
    const auto size = std::max(box_width, box_height) * crop_expansion_;
    const auto left = std::clamp(center_x - size * 0.5F, 0.0F, std::max(0.0F, static_cast<float>(image_width) - 1.0F));
    const auto top = std::clamp(center_y - size * 0.5F, 0.0F, std::max(0.0F, static_cast<float>(image_height) - 1.0F));
    const auto right = std::clamp(center_x + size * 0.5F, left + 1.0F, static_cast<float>(image_width));
    const auto bottom = std::clamp(center_y + size * 0.5F, top + 1.0F, static_cast<float>(image_height));
    const auto crop_size = std::min(right - left, bottom - top);
    return CropTransform{.left = left, .top = top, .size = std::max(1.0F, crop_size)};
  }

  auto HandPoseModel::extract_landmarks(const signlang::video_frontend::VideoFrameMetadata& metadata,
                                        const std::uint8_t* payload, std::uint64_t payload_size,
                                        const CropTransform& transform,
                                        std::array<HandPoseKeypoint, kHandPoseKeypointCount>& out, float& out_presence,
                                        bool& out_is_left) const -> bool {
    const auto src_rect = im_rect{
        .x = static_cast<int>(std::floor(transform.left)),
        .y = static_cast<int>(std::floor(transform.top)),
        .width = std::max(1, static_cast<int>(std::floor(transform.size))),
        .height = std::max(1, static_cast<int>(std::floor(transform.size))),
    };
    if (payload_size < checked_rgb_size_bytes(metadata.output_width, metadata.output_height)) {
      return false;
    }
    resize_rgb_with_rga(payload, metadata.output_width, metadata.output_height, landmark_model_->input_data(),
                        landmark_model_->width(), landmark_model_->height(), src_rect);
    landmark_model_->run();

    out_presence = std::clamp(landmark_model_->output_value(1, 0), 0.0F, 1.0F);
    if (out_presence < presence_threshold_) {
      return false;
    }

    if (landmark_model_->output_attrs.size() >= 3 && output_element_count(landmark_model_->output_attrs[2]) >= 1) {
      out_is_left = landmark_model_->output_value(2, 0) > handedness_threshold_;
    } else {
      out_is_left = false;
    }

    const auto scale = transform.size / static_cast<float>(landmark_model_->width());
    for (std::uint32_t i = 0; i < kHandPoseKeypointCount; ++i) {
      const auto base = static_cast<std::uint64_t>(i) * 3;
      const auto x = transform.left + landmark_model_->output_value(0, base) * scale;
      const auto y = transform.top + landmark_model_->output_value(0, base + 1) * scale;
      const auto z = landmark_model_->output_value(0, base + 2) * scale;
      out[i] = HandPoseKeypoint{
          .x = std::clamp(x, 0.0F, static_cast<float>(metadata.output_width)),
          .y = std::clamp(y, 0.0F, static_cast<float>(metadata.output_height)),
          .z = z,
          .confidence = out_presence,
      };
    }
    return true;
  }

  void HandPoseModel::run_landmark_detector(const signlang::video_frontend::VideoFrameMetadata& metadata,
                                            const std::uint8_t* payload, std::uint64_t payload_size,
                                            PalmCandidate& candidate) {
    const auto transform =
        crop_transform_from_palm_keypoints(candidate.palm_keypoints, metadata.output_width, metadata.output_height);
    std::array<HandPoseKeypoint, kHandPoseKeypointCount> keypoints{};
    float presence = 0.0F;
    bool is_left = false;

    if (!extract_landmarks(metadata, payload, payload_size, transform, keypoints, presence, is_left)) {
      candidate.detection.present = false;
      candidate.detection.presence_confidence = presence;
      return;
    }

    candidate.detection.keypoints = keypoints;
    candidate.detection.presence_confidence = presence;
    candidate.detection.confidence = std::min(candidate.detection.confidence, presence);
    candidate.detection.present = true;
    candidate.detection.is_left_hand = is_left;

    auto left = std::numeric_limits<float>::max();
    auto top = std::numeric_limits<float>::max();
    auto right = 0.0F;
    auto bottom = 0.0F;
    for (const auto& kp : keypoints) {
      left = std::min(left, kp.x);
      top = std::min(top, kp.y);
      right = std::max(right, kp.x);
      bottom = std::max(bottom, kp.y);
    }
    candidate.detection.box = HandPoseBox{.left = left, .top = top, .right = right, .bottom = bottom};
  }

  auto HandPoseModel::compute_iou(const HandPoseBox& box1, const HandPoseBox& box2) const -> float {
    const auto x1 = std::max(box1.left, box2.left);
    const auto y1 = std::max(box1.top, box2.top);
    const auto x2 = std::min(box1.right, box2.right);
    const auto y2 = std::min(box1.bottom, box2.bottom);

    if (x2 <= x1 || y2 <= y1) {
      return 0.0F;
    }

    const auto intersection = (x2 - x1) * (y2 - y1);
    const auto area1 = (box1.right - box1.left) * (box1.bottom - box1.top);
    const auto area2 = (box2.right - box2.left) * (box2.bottom - box2.top);
    const auto union_area = area1 + area2 - intersection;

    return union_area > 0.0F ? intersection / union_area : 0.0F;
  }

  void HandPoseModel::apply_nms() {
    if (candidates_.size() <= 1) {
      return;
    }

    std::sort(candidates_.begin(), candidates_.end(), [](const PalmCandidate& lhs, const PalmCandidate& rhs) {
      return lhs.detection.confidence > rhs.detection.confidence;
    });

    auto suppressed = std::vector<bool>(candidates_.size(), false);
    auto merged = std::vector<PalmCandidate>{};
    merged.reserve(candidates_.size());

    for (std::size_t i = 0; i < candidates_.size(); ++i) {
      if (suppressed[i]) {
        continue;
      }

      auto group = std::vector<std::size_t>{i};
      auto total_confidence = candidates_[i].detection.confidence;

      for (std::size_t j = i + 1; j < candidates_.size(); ++j) {
        if (suppressed[j]) {
          continue;
        }

        const auto iou = compute_iou(candidates_[i].detection.box, candidates_[j].detection.box);
        if (iou > nms_iou_threshold_) {
          group.push_back(j);
          total_confidence += candidates_[j].detection.confidence;
          suppressed[j] = true;
        }
      }

      if (group.size() == 1) {
        merged.push_back(candidates_[i]);
      } else {
        auto weighted = PalmCandidate{};
        weighted.detection.box.left = 0.0F;
        weighted.detection.box.top = 0.0F;
        weighted.detection.box.right = 0.0F;
        weighted.detection.box.bottom = 0.0F;
        weighted.detection.confidence = 0.0F;

        for (std::size_t k = 0; k < 14; ++k) {
          weighted.palm_keypoints[k] = 0.0F;
        }

        for (const auto idx : group) {
          const auto& candidate = candidates_[idx];
          const auto weight = candidate.detection.confidence / total_confidence;

          weighted.detection.box.left += candidate.detection.box.left * weight;
          weighted.detection.box.top += candidate.detection.box.top * weight;
          weighted.detection.box.right += candidate.detection.box.right * weight;
          weighted.detection.box.bottom += candidate.detection.box.bottom * weight;
          weighted.detection.confidence += candidate.detection.confidence * weight;

          for (std::size_t k = 0; k < 14; ++k) {
            weighted.palm_keypoints[k] += candidate.palm_keypoints[k] * weight;
          }
        }

        weighted.detection.class_id = candidates_[i].detection.class_id;
        weighted.detection.present = candidates_[i].detection.present;
        weighted.detection.is_left_hand = candidates_[i].detection.is_left_hand;
        weighted.detection.presence_confidence = candidates_[i].detection.presence_confidence;

        merged.push_back(weighted);
      }
    }

    candidates_ = std::move(merged);
  }

  auto HandPoseModel::crop_transform_from_palm_keypoints(const std::array<float, 14>& palm_keypoints,
                                                         std::uint32_t image_width, std::uint32_t image_height) const
      -> CropTransform {
    constexpr auto kWristIdx = std::uint32_t{0};
    constexpr auto kMiddleMcpIdx = std::uint32_t{2};
    constexpr auto kIndexMcpIdx = std::uint32_t{4};
    constexpr auto kRingMcpIdx = std::uint32_t{6};

    const auto wrist_x = palm_keypoints[kWristIdx * 2];
    const auto wrist_y = palm_keypoints[kWristIdx * 2 + 1];
    const auto middle_mcp_x = palm_keypoints[kMiddleMcpIdx * 2];
    const auto middle_mcp_y = palm_keypoints[kMiddleMcpIdx * 2 + 1];

    const auto palm_center_x = (wrist_x + middle_mcp_x) * 0.5F;
    const auto palm_center_y = (wrist_y + middle_mcp_y) * 0.5F;

    const auto dx = middle_mcp_x - wrist_x;
    const auto dy = middle_mcp_y - wrist_y;
    const auto palm_length = std::sqrt(dx * dx + dy * dy);

    const auto index_mcp_x = palm_keypoints[kIndexMcpIdx * 2];
    const auto index_mcp_y = palm_keypoints[kIndexMcpIdx * 2 + 1];
    const auto ring_mcp_x = palm_keypoints[kRingMcpIdx * 2];
    const auto ring_mcp_y = palm_keypoints[kRingMcpIdx * 2 + 1];
    const auto palm_width_dx = ring_mcp_x - index_mcp_x;
    const auto palm_width_dy = ring_mcp_y - index_mcp_y;
    const auto palm_width = std::sqrt(palm_width_dx * palm_width_dx + palm_width_dy * palm_width_dy);

    const auto palm_size = std::max(palm_length, palm_width);
    auto expansion = base_roi_expansion_;

    if (palm_size < small_hand_threshold_) {
      expansion = small_hand_expansion_;
    } else if (palm_size > large_hand_threshold_) {
      expansion = large_hand_expansion_;
    }

    const auto dist_to_left = palm_center_x;
    const auto dist_to_right = static_cast<float>(image_width) - palm_center_x;
    const auto dist_to_top = palm_center_y;
    const auto dist_to_bottom = static_cast<float>(image_height) - palm_center_y;
    const auto min_boundary_dist = std::min({dist_to_left, dist_to_right, dist_to_top, dist_to_bottom});

    if (min_boundary_dist < boundary_margin_) {
      const auto boundary_factor = std::clamp(min_boundary_dist / boundary_margin_, boundary_min_factor_, 1.0F);
      expansion *= boundary_factor;
    }

    const auto roi_size = palm_size * expansion;
    const auto left =
        std::clamp(palm_center_x - roi_size * 0.5F, 0.0F, std::max(0.0F, static_cast<float>(image_width) - 1.0F));
    const auto top =
        std::clamp(palm_center_y - roi_size * 0.5F, 0.0F, std::max(0.0F, static_cast<float>(image_height) - 1.0F));
    const auto right = std::clamp(palm_center_x + roi_size * 0.5F, left + 1.0F, static_cast<float>(image_width));
    const auto bottom = std::clamp(palm_center_y + roi_size * 0.5F, top + 1.0F, static_cast<float>(image_height));
    const auto crop_size = std::min(right - left, bottom - top);

    return CropTransform{.left = left, .top = top, .size = std::max(1.0F, crop_size)};
  }

  void HandPoseModel::try_tracking_from_previous_frame(const signlang::video_frontend::VideoFrameMetadata& metadata,
                                                       const std::uint8_t* payload, std::uint64_t payload_size) {
    for (auto& tracked : tracked_hands_) {
      if (current_frame_number_ - tracked.last_seen_frame > max_tracking_gap_) {
        continue;
      }
      if (tracked.presence_confidence < tracking_threshold_) {
        continue;
      }

      const auto transform = crop_transform_from_box(tracked.roi, metadata.output_width, metadata.output_height);
      std::array<HandPoseKeypoint, kHandPoseKeypointCount> keypoints{};
      float presence = 0.0F;
      bool is_left = false;

      if (!extract_landmarks(metadata, payload, payload_size, transform, keypoints, presence, is_left)) {
        continue;
      }

      tracked.smoothed_keypoints = keypoints;
      tracked.palm_confidence = std::min(tracked.palm_confidence, presence);
      tracked.presence_confidence = presence;
      tracked.is_left_hand = is_left;
      tracked.last_seen_frame = current_frame_number_;

      auto left = std::numeric_limits<float>::max();
      auto top = std::numeric_limits<float>::max();
      auto right = 0.0F;
      auto bottom = 0.0F;
      for (const auto& kp : keypoints) {
        left = std::min(left, kp.x);
        top = std::min(top, kp.y);
        right = std::max(right, kp.x);
        bottom = std::max(bottom, kp.y);
      }
      tracked.roi = HandPoseBox{.left = left, .top = top, .right = right, .bottom = bottom};
    }
  }

  void HandPoseModel::update_tracked_hands(const std::vector<PalmCandidate>& detected_hands) {
    for (const auto& detected : detected_hands) {
      if (!detected.detection.present) {
        continue;
      }

      auto matched_idx = -1;
      auto best_iou = tracking_iou_match_threshold_;

      for (std::size_t i = 0; i < tracked_hands_.size(); ++i) {
        if (tracked_hands_[i].last_seen_frame != current_frame_number_) {
          continue;
        }
        const auto iou = compute_iou(tracked_hands_[i].roi, detected.detection.box);
        if (iou > best_iou) {
          best_iou = iou;
          matched_idx = static_cast<int>(i);
        }
      }

      if (matched_idx >= 0) {
        auto& t = tracked_hands_[static_cast<std::size_t>(matched_idx)];
        t.roi = detected.detection.box;
        t.smoothed_keypoints = detected.detection.keypoints;
        t.palm_confidence = detected.detection.confidence;
        t.presence_confidence = detected.detection.presence_confidence;
        t.is_left_hand = detected.detection.is_left_hand;
      } else {
        auto new_hand = TrackedHand{
            .roi = detected.detection.box,
            .palm_confidence = detected.detection.confidence,
            .presence_confidence = detected.detection.presence_confidence,
            .last_seen_frame = current_frame_number_,
            .is_left_hand = detected.detection.is_left_hand,
            .smoothed_keypoints = detected.detection.keypoints,
        };

        auto added = false;
        for (auto& tracked : tracked_hands_) {
          if (current_frame_number_ - tracked.last_seen_frame > max_stale_frames_) {
            tracked = new_hand;
            added = true;
            break;
          }
        }

        if (!added && tracked_hands_.size() < output_hands_) {
          tracked_hands_.push_back(new_hand);
        }
      }
    }
  }

  auto HandPoseModel::apply_one_euro_filter(OneEuroFilter& filter, float value, std::uint64_t timestamp_ns) -> float {
    if (!filter.initialized) {
      filter.x = value;
      filter.dx = 0.0F;
      filter.last_time = timestamp_ns;
      filter.initialized = true;
      return value;
    }

    const auto dt = static_cast<float>(timestamp_ns - filter.last_time) / 1e9F;
    if (dt <= 0.0F || dt > 1.0F) {
      return value;
    }

    const auto alpha_d = 1.0F / (1.0F + (1.0F / (2.0F * kPi * filter.d_cutoff * dt)));
    filter.dx = alpha_d * ((value - filter.x) / dt) + (1.0F - alpha_d) * filter.dx;

    const auto cutoff = filter.min_cutoff + filter.beta * std::abs(filter.dx);
    const auto alpha = 1.0F / (1.0F + (1.0F / (2.0F * kPi * cutoff * dt)));

    filter.x = alpha * value + (1.0F - alpha) * filter.x;
    filter.last_time = timestamp_ns;

    return filter.x;
  }

  void HandPoseModel::smooth_keypoints_hand(std::size_t hand_index, std::uint64_t timestamp_ns) {
    if (hand_index >= keypoint_filters_.size() || hand_index >= tracked_hands_.size()) {
      return;
    }

    auto& filters = keypoint_filters_[hand_index];
    auto& keypoints = tracked_hands_[hand_index].smoothed_keypoints;

    if (filters.size() < kHandPoseKeypointCount * 3) {
      return;
    }

    for (std::uint32_t i = 0; i < kHandPoseKeypointCount; ++i) {
      keypoints[i].x = apply_one_euro_filter(filters[i * 3], keypoints[i].x, timestamp_ns);
      keypoints[i].y = apply_one_euro_filter(filters[i * 3 + 1], keypoints[i].y, timestamp_ns);
      keypoints[i].z = apply_one_euro_filter(filters[i * 3 + 2], keypoints[i].z, timestamp_ns);
    }
  }

} // namespace signlang::handpose_det
