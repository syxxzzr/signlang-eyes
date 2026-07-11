#include "signlang_model.hpp"

#include "feature_extractor.hpp"
#include "prototype_database.hpp"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace signlang::signlang_det {

  auto PrototypeStore::gestures() const -> const std::vector<GesturePrototypeSet>& { return gestures_; }
  auto PrototypeStore::gesture_count() const -> std::size_t { return gestures_.size(); }
  auto PrototypeStore::sample_count() const -> std::size_t { return sample_count_; }

  auto PrototypeStore::gesture_name(std::uint32_t gesture_id) const -> const char* {
    const auto found = names_.find(gesture_id);
    return found == names_.end() ? "unknown" : found->second.c_str();
  }

  void PrototypeStore::replace(std::vector<GesturePrototypeSet> gestures) {
    gestures_ = std::move(gestures);
    names_.clear();
    sample_count_ = 0;
    for (const auto& gesture : gestures_) {
      names_.emplace(gesture.gesture_id, gesture.name);
      sample_count_ += gesture.samples.size();
    }
  }

  namespace {

    class OutputGuard {
    public:
      OutputGuard(rknn_context context, rknn_output* output) : context_{context}, output_{output} {}
      ~OutputGuard() { static_cast<void>(rknn_outputs_release(context_, 1, output_)); }
    private:
      rknn_context context_;
      rknn_output* output_;
    };

    auto load_model_file(const std::string& path) -> std::vector<unsigned char> {
      auto file = std::ifstream{path, std::ios::binary | std::ios::ate};
      if (!file) throw std::runtime_error("failed to open temporal encoder: " + path);
      const auto size = file.tellg();
      if (size <= 0) throw std::runtime_error("temporal encoder file is empty: " + path);
      file.seekg(0);
      auto bytes = std::vector<unsigned char>(static_cast<std::size_t>(size));
      if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        throw std::runtime_error("failed to read temporal encoder: " + path);
      }
      return bytes;
    }

    auto embedding_norm(const FrameEmbedding& value) -> float {
      auto sum = 0.0F;
      for (const auto item : value) {
        if (!std::isfinite(item)) throw std::runtime_error("temporal encoder produced a non-finite value");
        sum += item * item;
      }
      return std::sqrt(sum);
    }

    void normalize(FrameEmbedding& value) {
      const auto length = embedding_norm(value);
      if (length <= 1e-8F) throw std::runtime_error("temporal encoder produced a zero embedding");
      for (auto& item : value) item /= length;
    }

    class TemporalEncoder {
    public:
      TemporalEncoder(const std::string& path, rknn_core_mask core) {
        const auto model = load_model_file(path);
        auto result = rknn_init(&context_, const_cast<unsigned char*>(model.data()),
                                static_cast<std::uint32_t>(model.size()), 0, nullptr);
        if (result != RKNN_SUCC) throw std::runtime_error("rknn_init failed, ret=" + std::to_string(result));
        try {
          result = rknn_set_core_mask(context_, core);
          if (result != RKNN_SUCC) spdlog::warn("rknn_set_core_mask failed, ret={}", result);
          validate_contract();
        } catch (...) {
          static_cast<void>(rknn_destroy(context_));
          context_ = 0;
          throw;
        }
      }

      ~TemporalEncoder() { if (context_ != 0) static_cast<void>(rknn_destroy(context_)); }
      auto encode(const PackedSegment& segment) -> EncodedGesture {
        if (segment.valid_length == 0 || segment.valid_length > kMaxSequenceLength) {
          throw std::runtime_error("invalid packed segment length");
        }
        auto input = rknn_input{};
        input.index = 0;
        input.type = RKNN_TENSOR_FLOAT32;
        input.fmt = RKNN_TENSOR_NCHW;
        input.size = static_cast<std::uint32_t>(segment.landmarks.size() * sizeof(float));
        input.buf = const_cast<float*>(segment.landmarks.data());
        auto result = rknn_inputs_set(context_, 1, &input);
        if (result != RKNN_SUCC || (result = rknn_run(context_, nullptr)) != RKNN_SUCC) {
          throw std::runtime_error("temporal encoder execution failed, ret=" + std::to_string(result));
        }
        auto output = rknn_output{};
        output.want_float = 1;
        result = rknn_outputs_get(context_, 1, &output, nullptr);
        if (result != RKNN_SUCC) {
          throw std::runtime_error("failed to retrieve temporal encoder output, ret=" + std::to_string(result));
        }
        const auto guard = OutputGuard{context_, &output};
        if (output.size != kMaxSequenceLength * kEmbeddingDim * sizeof(float)) {
          throw std::runtime_error("temporal encoder output byte size mismatch");
        }
        const auto* data = static_cast<const float*>(output.buf);
        auto encoded = EncodedGesture{};
        encoded.segment = segment;
        encoded.frames.resize(segment.valid_length);
        for (std::uint32_t frame = 0; frame < segment.valid_length; ++frame) {
          std::copy_n(data + static_cast<std::size_t>(frame) * kEmbeddingDim, kEmbeddingDim,
                      encoded.frames[frame].begin());
          if (std::abs(embedding_norm(encoded.frames[frame]) - 1.0F) > 0.05F) {
            throw std::runtime_error("temporal encoder frame embedding is not L2-normalized");
          }
          for (std::size_t dimension = 0; dimension < kEmbeddingDim; ++dimension) {
            encoded.pooled[dimension] += encoded.frames[frame][dimension];
          }
        }
        for (auto& value : encoded.pooled) value /= static_cast<float>(segment.valid_length);
        normalize(encoded.pooled);
        return encoded;
      }

    private:
      void validate_contract() {
        auto counts = rknn_input_output_num{};
        if (rknn_query(context_, RKNN_QUERY_IN_OUT_NUM, &counts, sizeof(counts)) != RKNN_SUCC ||
            counts.n_input != 1 || counts.n_output != 1) {
          throw std::runtime_error("temporal encoder must expose one input and one output");
        }
        auto input = rknn_tensor_attr{};
        auto output = rknn_tensor_attr{};
        if (rknn_query(context_, RKNN_QUERY_INPUT_ATTR, &input, sizeof(input)) != RKNN_SUCC ||
            rknn_query(context_, RKNN_QUERY_OUTPUT_ATTR, &output, sizeof(output)) != RKNN_SUCC) {
          throw std::runtime_error("failed to query temporal encoder tensor attributes");
        }
        const auto input_ok = std::strcmp(input.name, "landmarks") == 0 && input.type == RKNN_TENSOR_FLOAT32 &&
            input.fmt == RKNN_TENSOR_NCHW && input.n_dims == 3 && input.dims[0] == 1 &&
            input.dims[1] == kMaxSequenceLength && input.dims[2] == kFeatureDim;
        const auto output_ok = std::strcmp(output.name, "frame_embeddings") == 0 &&
            output.type == RKNN_TENSOR_FLOAT32 && output.fmt == RKNN_TENSOR_NCHW && output.n_dims == 3 &&
            output.dims[0] == 1 && output.dims[1] == kMaxSequenceLength && output.dims[2] == kEmbeddingDim;
        if (!input_ok || !output_ok) throw std::runtime_error("temporal encoder tensor contract mismatch");
      }

      rknn_context context_{0};
    };

    class PrototypeMatcher {
    public:
      explicit PrototypeMatcher(MatcherOptions options) : options_{options} {
        if (options_.top_k == 0 || !std::isfinite(options_.dtw_window_ratio) ||
            options_.dtw_window_ratio < 0.0F || options_.dtw_window_ratio > 1.0F ||
            !std::isfinite(options_.global_dtw_threshold) || options_.global_dtw_threshold < 0.0F ||
            !std::isfinite(options_.global_coarse_threshold) || options_.global_coarse_threshold < 0.0F ||
            !std::isfinite(options_.margin_threshold) || options_.margin_threshold < 0.0F ||
            !std::isfinite(options_.confidence_slope) || !std::isfinite(options_.confidence_intercept)) {
          throw std::invalid_argument("invalid prototype matcher options");
        }
      }

      auto dtw(const std::vector<FrameEmbedding>& left, const std::vector<FrameEmbedding>& right) const -> float {
        if (left.empty() || right.empty()) return std::numeric_limits<float>::infinity();
        const auto difference = left.size() > right.size() ? left.size() - right.size() : right.size() - left.size();
        const auto band = std::max<std::size_t>(difference, std::max<std::size_t>(1,
            static_cast<std::size_t>(std::lround(options_.dtw_window_ratio * std::max(left.size(), right.size())))));
        auto previous_cost = std::vector<float>(right.size() + 1U, std::numeric_limits<float>::infinity());
        auto previous_steps = std::vector<std::uint32_t>(right.size() + 1U, 0);
        previous_cost[0] = 0.0F;
        for (std::size_t row = 1; row <= left.size(); ++row) {
          auto current_cost = std::vector<float>(right.size() + 1U, std::numeric_limits<float>::infinity());
          auto current_steps = std::vector<std::uint32_t>(right.size() + 1U, 0);
          const auto first = row > band ? row - band : 1U;
          const auto last = std::min(right.size(), row + band);
          for (auto column = first; column <= last; ++column) {
            const auto alternatives = std::array<std::pair<float, std::uint32_t>, 3>{{
                {previous_cost[column], previous_steps[column]},
                {current_cost[column - 1U], current_steps[column - 1U]},
                {previous_cost[column - 1U], previous_steps[column - 1U]}}};
            const auto best = *std::min_element(alternatives.begin(), alternatives.end(),
                [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
            current_cost[column] = best.first + frame_distance(left[row - 1U], right[column - 1U]);
            current_steps[column] = best.second + 1U;
          }
          previous_cost = std::move(current_cost);
          previous_steps = std::move(current_steps);
        }
        return previous_steps[right.size()] == 0 ? std::numeric_limits<float>::infinity()
            : previous_cost[right.size()] / static_cast<float>(previous_steps[right.size()]);
      }

      auto match(const EncodedGesture& query, const PrototypeStore& store) const -> MatchResult {
        auto result = MatchResult{};
        if (store.gestures().empty()) return result;
        struct Coarse { const GesturePrototypeSet* gesture; float distance; };
        auto coarse = std::vector<Coarse>{};
        for (const auto& gesture : store.gestures()) {
          auto best = std::numeric_limits<float>::infinity();
          for (const auto& sample : gesture.samples) best = std::min(best, cosine(query.pooled, sample.pooled));
          coarse.push_back({&gesture, best});
        }
        std::sort(coarse.begin(), coarse.end(), [](const auto& lhs, const auto& rhs) {
          return lhs.distance == rhs.distance ? lhs.gesture->gesture_id < rhs.gesture->gesture_id
                                              : lhs.distance < rhs.distance;
        });
        coarse.resize(std::min<std::size_t>(coarse.size(), options_.top_k));
        for (const auto& coarse_candidate : coarse) {
          auto best = MatchCandidate{coarse_candidate.gesture->gesture_id, 0, coarse_candidate.distance,
                                     std::numeric_limits<float>::infinity(), 0.0F};
          for (const auto& sample : coarse_candidate.gesture->samples) {
            const auto distance = dtw(query.frames, sample.frames);
            if (distance < best.dtw_distance) {
              best.sample_id = sample.sample_id;
              best.dtw_distance = distance;
            }
          }
          if (options_.confidence_mapping_enabled && std::isfinite(best.dtw_distance)) {
            best.confidence = 1.0F / (1.0F + std::exp(-(options_.confidence_slope * best.dtw_distance +
                                                       options_.confidence_intercept)));
          }
          result.candidates.push_back(best);
        }
        std::sort(result.candidates.begin(), result.candidates.end(), [](const auto& lhs, const auto& rhs) {
          return lhs.dtw_distance == rhs.dtw_distance ? lhs.gesture_id < rhs.gesture_id
                                                      : lhs.dtw_distance < rhs.dtw_distance;
        });
        const auto& top = result.candidates.front();
        const auto* gesture = coarse.front().gesture;
        for (const auto& item : store.gestures()) if (item.gesture_id == top.gesture_id) { gesture = &item; break; }
        result.gesture_id = top.gesture_id;
        result.top1_dtw_distance = top.dtw_distance;
        result.top2_dtw_distance = result.candidates.size() > 1U ? result.candidates[1].dtw_distance
                                                                 : std::numeric_limits<float>::infinity();
        result.distance_margin = result.candidates.size() > 1U ? result.top2_dtw_distance - result.top1_dtw_distance
                                                                : std::numeric_limits<float>::infinity();
        result.coarse_distance = top.coarse_distance;
        result.applied_dtw_threshold = gesture->calibration == CalibrationStatus::Calibrated
            ? gesture->dtw_threshold : options_.global_dtw_threshold;
        result.applied_coarse_threshold = gesture->calibration == CalibrationStatus::Calibrated
            ? gesture->coarse_threshold : options_.global_coarse_threshold;
        if (result.top1_dtw_distance > result.applied_dtw_threshold) {
          result.rejection_reason = RejectionReason::DtwDistance;
        } else if (result.candidates.size() > 1U && result.distance_margin < options_.margin_threshold) {
          result.rejection_reason = RejectionReason::DistanceMargin;
        } else if (options_.use_coarse_threshold && result.coarse_distance > result.applied_coarse_threshold) {
          result.rejection_reason = RejectionReason::CoarseDistance;
        } else {
          result.recognized = true;
          result.rejection_reason = RejectionReason::None;
        }
        return result;
      }

    private:
      static auto frame_distance(const FrameEmbedding& lhs, const FrameEmbedding& rhs) -> float {
        auto sum = 0.0F;
        for (std::size_t index = 0; index < lhs.size(); ++index) {
          const auto difference = lhs[index] - rhs[index];
          sum += difference * difference;
        }
        return std::sqrt(sum);
      }

      static auto cosine(const FrameEmbedding& lhs, const FrameEmbedding& rhs) -> float {
        auto dot = 0.0F;
        for (std::size_t index = 0; index < lhs.size(); ++index) dot += lhs[index] * rhs[index];
        return 1.0F - std::clamp(dot, -1.0F, 1.0F);
      }

      MatcherOptions options_;
    };

  } // namespace

  class SignlangModel::Impl {
  public:
    Impl(const std::string& model_path, rknn_core_mask core, PreprocessingOptions preprocessing,
         MatcherOptions matcher, float confidence) :
        preprocessor{preprocessing}, encoder{model_path, core}, prototype_matcher{matcher},
        min_keypoint_confidence{confidence} {}

    auto process(const GestureSegment& segment) -> EncodedGesture {
      return encoder.encode(preprocessor.prepare(preprocessor.evaluate(segment.frames, segment.forced_max_length)));
    }

    auto process_recording(const std::vector<RecordedHandposeFrame>& frames) -> EncodedGesture {
      auto extractor = FeatureExtractor{min_keypoint_confidence};
      auto features = std::vector<FeatureVector>{};
      features.reserve(frames.size());
      for (const auto& frame : frames) {
        features.push_back(extractor.extract(frame.metadata, frame.detections.data(), frame.detection_count));
      }
      return process(preprocessor.evaluate(std::move(features), false));
    }

    SegmentPreprocessor preprocessor;
    TemporalEncoder encoder;
    PrototypeMatcher prototype_matcher;
    PrototypeStore prototypes;
    float min_keypoint_confidence;
  };

  SignlangModel::SignlangModel(const std::string& model_path, const std::string& prototypes_path,
      rknn_core_mask core, PreprocessingOptions preprocessing, MatcherOptions matcher, float confidence) :
      impl_{std::make_unique<Impl>(model_path, core, preprocessing, matcher, confidence)} {
    auto database = PrototypeDatabase{prototypes_path};
    database.ensure_valid_empty_or_existing();
    impl_->prototypes = database.load_store();
    spdlog::info("Loaded {} prototype samples across {} gestures", impl_->prototypes.sample_count(),
                 impl_->prototypes.gesture_count());
  }

  SignlangModel::~SignlangModel() = default;

  auto SignlangModel::infer(const GestureSegment& segment) -> InferenceResult {
    const auto started = std::chrono::steady_clock::now();
    auto encoded = impl_->process(segment);
    auto result = InferenceResult{impl_->prototype_matcher.match(encoded, impl_->prototypes), encoded.segment, 0.0F};
    result.inference_time_ms = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return result;
  }

  auto SignlangModel::encode_recording(const std::vector<RecordedHandposeFrame>& frames) -> EncodedGesture {
    return impl_->process_recording(frames);
  }

  auto SignlangModel::dtw_distance(const std::vector<FrameEmbedding>& lhs,
                                   const std::vector<FrameEmbedding>& rhs) const -> float {
    return impl_->prototype_matcher.dtw(lhs, rhs);
  }

  auto SignlangModel::get_gesture_name(std::uint32_t id) const -> const char* {
    return impl_->prototypes.gesture_name(id);
  }

  auto SignlangModel::expected_sequence_length() const -> std::uint32_t { return kMaxSequenceLength; }
  auto SignlangModel::embedding_dim() const -> std::uint32_t { return kEmbeddingDim; }
  auto SignlangModel::loaded_gesture_count() const -> std::size_t { return impl_->prototypes.gesture_count(); }
  auto SignlangModel::loaded_sample_count() const -> std::size_t { return impl_->prototypes.sample_count(); }

  void SignlangModel::reload_prototypes(const std::string& path) {
    auto database = PrototypeDatabase{path};
    database.ensure_valid_empty_or_existing();
    impl_->prototypes = database.load_store();
    spdlog::info("Reloaded {} prototype samples across {} gestures", impl_->prototypes.sample_count(),
                 impl_->prototypes.gesture_count());
  }

} // namespace signlang::signlang_det
