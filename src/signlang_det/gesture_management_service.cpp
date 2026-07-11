#include "gesture_management_service.hpp"

#include "common/fixed_string.hpp"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <limits>
#include <stdexcept>
#include <utility>

namespace signlang::signlang_det {
  namespace {

    using Matrix = std::vector<std::vector<float>>;

    auto median(std::vector<float> values) -> float {
      if (values.empty()) return 0.0F;
      const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2U);
      std::nth_element(values.begin(), middle, values.end());
      auto result = *middle;
      if (values.size() % 2U == 0U) result = (result + *std::max_element(values.begin(), middle)) * 0.5F;
      return result;
    }

    auto distances(SignlangModel& model, const std::vector<GesturePrototype>& samples) -> Matrix {
      auto result = Matrix(samples.size(), std::vector<float>(samples.size(), 0.0F));
      for (std::size_t left = 0; left < samples.size(); ++left) {
        for (std::size_t right = left + 1U; right < samples.size(); ++right) {
          result[left][right] = model.dtw_distance(samples[left].frames, samples[right].frames);
          result[right][left] = result[left][right];
        }
      }
      return result;
    }

    auto filter_outliers(const std::vector<GesturePrototype>& samples, const Matrix& matrix)
        -> std::vector<GesturePrototype> {
      if (samples.size() < 4U) return samples;
      auto means = std::vector<float>(samples.size(), 0.0F);
      for (std::size_t row = 0; row < samples.size(); ++row) {
        for (std::size_t column = 0; column < samples.size(); ++column) {
          if (row != column) means[row] += matrix[row][column];
        }
        means[row] /= static_cast<float>(samples.size() - 1U);
      }
      const auto center = median(means);
      auto deviations = std::vector<float>{};
      for (const auto value : means) deviations.push_back(std::abs(value - center));
      const auto mad = median(deviations);
      if (mad <= 1e-6F) return samples;
      const auto limit = center + 3.0F * 1.4826F * mad;
      auto result = std::vector<GesturePrototype>{};
      for (std::size_t index = 0; index < samples.size(); ++index) {
        if (means[index] <= limit) result.push_back(samples[index]);
      }
      return result.empty() ? samples : result;
    }

    auto select_medoids(const std::vector<GesturePrototype>& samples, const Matrix& matrix, std::size_t limit)
        -> std::vector<GesturePrototype> {
      if (samples.size() <= limit) return samples;
      auto selected = std::vector<std::size_t>{};
      while (selected.size() < limit) {
        auto best_index = std::size_t{0};
        auto best_cost = std::numeric_limits<float>::infinity();
        for (std::size_t candidate = 0; candidate < samples.size(); ++candidate) {
          if (std::find(selected.begin(), selected.end(), candidate) != selected.end()) continue;
          auto trial = selected;
          trial.push_back(candidate);
          auto cost = 0.0F;
          for (std::size_t sample = 0; sample < samples.size(); ++sample) {
            auto nearest = std::numeric_limits<float>::infinity();
            for (const auto medoid : trial) nearest = std::min(nearest, matrix[sample][medoid]);
            cost += nearest;
          }
          if (cost < best_cost) { best_cost = cost; best_index = candidate; }
        }
        selected.push_back(best_index);
      }
      std::sort(selected.begin(), selected.end());
      auto result = std::vector<GesturePrototype>{};
      for (const auto index : selected) result.push_back(samples[index]);
      return result;
    }

    auto calibrated_threshold(const Matrix& matrix, float fallback) -> float {
      if (matrix.size() < 2U) return fallback;
      auto leave_one_out = std::vector<float>{};
      for (std::size_t row = 0; row < matrix.size(); ++row) {
        auto nearest = std::numeric_limits<float>::infinity();
        for (std::size_t column = 0; column < matrix.size(); ++column) {
          if (row != column) nearest = std::min(nearest, matrix[row][column]);
        }
        leave_one_out.push_back(nearest);
      }
      return std::max(1e-4F, *std::max_element(leave_one_out.begin(), leave_one_out.end()) * 1.25F);
    }

    auto pooled_threshold(const std::vector<GesturePrototype>& samples, float fallback) -> float {
      if (samples.size() < 2U) return fallback;
      auto greatest_nearest = 0.0F;
      for (std::size_t left = 0; left < samples.size(); ++left) {
        auto nearest = std::numeric_limits<float>::infinity();
        for (std::size_t right = 0; right < samples.size(); ++right) {
          if (left == right) continue;
          auto dot = 0.0F;
          for (std::size_t dimension = 0; dimension < kEmbeddingDim; ++dimension) {
            dot += samples[left].pooled[dimension] * samples[right].pooled[dimension];
          }
          nearest = std::min(nearest, 1.0F - std::clamp(dot, -1.0F, 1.0F));
        }
        greatest_nearest = std::max(greatest_nearest, nearest);
      }
      return std::max(1e-4F, greatest_nearest * 1.25F);
    }

  } // namespace

  GestureManagementService::GestureManagementService(const ProgramOptions& options, SignlangModel& model,
                                                       std::mutex& model_mutex) :
      options_{options}, model_{model}, model_mutex_{model_mutex},
      database_{options.prototypes_path} {
    database_.ensure_valid_empty_or_existing();
  }

  auto GestureManagementService::handle_request(const GestureManagementRequest& request) -> GestureManagementResponse {
    try {
      switch (request.command) {
      case GestureManagementCommand::GetStatus: return handle_get_status(request);
      case GestureManagementCommand::ListGestures: return handle_list_gestures(request);
      case GestureManagementCommand::AddGestureBegin: return handle_add_begin(request);
      case GestureManagementCommand::AddGestureChunk: return handle_add_chunk(request);
      case GestureManagementCommand::AddGestureCommit: return handle_add_commit(request);
      case GestureManagementCommand::AddGestureAbort: return handle_add_abort(request);
      case GestureManagementCommand::DeleteGestureById:
      case GestureManagementCommand::DeleteGestureByName: return handle_delete_gesture(request);
      default: return make_response(request, GestureManagementStatus::UnsupportedCommand, "unsupported command");
      }
    } catch (const std::exception& error) {
      return make_response(request, GestureManagementStatus::BadRequest, error.what());
    }
  }

  auto GestureManagementService::make_response(const GestureManagementRequest& request, GestureManagementStatus status,
                                                 const std::string& message) const -> GestureManagementResponse {
    auto response = GestureManagementResponse{};
    response.status = status;
    response.request_id = request.request_id;
    const std::lock_guard lock{model_mutex_};
    response.sequence_length = model_.expected_sequence_length();
    response.embedding_dim = model_.embedding_dim();
    response.loaded_gesture_count = static_cast<std::uint32_t>(model_.loaded_gesture_count());
    response.loaded_sample_count = static_cast<std::uint32_t>(model_.loaded_sample_count());
    common::copy_fixed_string(message, response.message);
    return response;
  }

  auto GestureManagementService::handle_get_status(const GestureManagementRequest& request) -> GestureManagementResponse {
    return make_response(request, GestureManagementStatus::Ok, "ok");
  }

  auto GestureManagementService::handle_list_gestures(const GestureManagementRequest& request) -> GestureManagementResponse {
    auto response = make_response(request, GestureManagementStatus::Ok);
    const auto gestures = database_.list_gestures();
    response.gesture_count = static_cast<std::uint32_t>(std::min<std::size_t>(gestures.size(), kGestureManagementMaxGestures));
    for (std::size_t index = 0; index < response.gesture_count; ++index) {
      response.gestures[index].id = gestures[index].id;
      response.gestures[index].sample_count = gestures[index].sample_count;
      response.gestures[index].enabled = gestures[index].enabled;
      response.gestures[index].calibrated = gestures[index].calibration == CalibrationStatus::Calibrated;
      common::copy_fixed_string(gestures[index].name, response.gestures[index].name);
    }
    return response;
  }

  auto GestureManagementService::handle_delete_gesture(const GestureManagementRequest& request) -> GestureManagementResponse {
    const auto removed = request.command == GestureManagementCommand::DeleteGestureById
        ? database_.delete_gesture(request.gesture_id)
        : database_.delete_gesture(common::fixed_string_to_string(request.gesture_name));
    if (!removed) return make_response(request, GestureManagementStatus::NotFound, "gesture not found");
    reload_model_prototypes();
    return make_response(request, GestureManagementStatus::Ok);
  }

  auto GestureManagementService::handle_add_begin(const GestureManagementRequest& request) -> GestureManagementResponse {
    const auto name = common::fixed_string_to_string(request.gesture_name);
    if (name.empty() || request.frame_count == 0 || request.frame_count > kMaxCapturedFrames) {
      throw std::runtime_error("invalid uploaded gesture name or frame count");
    }
    upload_ = UploadSession{request.transfer_id, name, request.replace_existing, request.frame_count,
                            std::vector<RecordedHandposeFrame>(request.frame_count),
                            std::vector<std::uint8_t>(request.frame_count, 0)};
    return make_response(request, GestureManagementStatus::Ok);
  }

  auto GestureManagementService::handle_add_chunk(const GestureManagementRequest& request) -> GestureManagementResponse {
    if (!upload_ || request.transfer_id != upload_->transfer_id || request.frame_index >= upload_->frame_count ||
        request.detection_count > kMaxHandCount) {
      throw std::runtime_error("invalid uploaded gesture chunk");
    }
    upload_->frames[request.frame_index] = RecordedHandposeFrame{request.frame_metadata, request.detections,
                                                                 request.detection_count};
    upload_->received[request.frame_index] = 1;
    return make_response(request, GestureManagementStatus::Ok);
  }

  auto GestureManagementService::handle_add_commit(const GestureManagementRequest& request) -> GestureManagementResponse {
    if (!upload_ || request.transfer_id != upload_->transfer_id ||
        !std::all_of(upload_->received.begin(), upload_->received.end(), [](auto value) { return value != 0; })) {
      throw std::runtime_error("uploaded gesture is incomplete");
    }
    const auto id = encode_uploaded_gesture(*upload_);
    upload_.reset();
    reload_model_prototypes();
    auto response = make_response(request, GestureManagementStatus::Ok);
    response.gesture_id = id;
    return response;
  }

  auto GestureManagementService::handle_add_abort(const GestureManagementRequest& request) -> GestureManagementResponse {
    upload_.reset();
    return make_response(request, GestureManagementStatus::Ok);
  }

  void GestureManagementService::reload_model_prototypes() {
    const std::lock_guard lock{model_mutex_};
    model_.reload_prototypes(options_.prototypes_path);
  }

  auto GestureManagementService::encode_uploaded_gesture(const UploadSession& session) -> std::uint32_t {
    EncodedGesture encoded;
    {
      const std::lock_guard lock{model_mutex_};
      encoded = model_.encode_recording(session.frames);
    }
    auto candidates = session.replace_existing ? std::vector<GesturePrototype>{}
                                               : database_.load_gesture_samples(session.gesture_name);
    candidates.push_back(GesturePrototype{0, std::move(encoded.frames), encoded.pooled,
        encoded.segment.valid_length, encoded.segment.quality.score,
        encoded.segment.end_timestamp_ns / 1'000'000'000U});
    Matrix matrix;
    {
      const std::lock_guard lock{model_mutex_};
      matrix = distances(model_, candidates);
      candidates = filter_outliers(candidates, matrix);
      matrix = distances(model_, candidates);
      candidates = select_medoids(candidates, matrix,
          std::min<std::size_t>(options_.max_representative_samples, candidates.size()));
      matrix = distances(model_, candidates);
    }
    const auto calibration = candidates.size() > 1U ? CalibrationStatus::Calibrated : CalibrationStatus::Uncalibrated;
    const auto threshold = calibrated_threshold(matrix, options_.matcher.global_dtw_threshold);
    const auto coarse_threshold = pooled_threshold(candidates, options_.matcher.global_coarse_threshold);
    const auto id = database_.replace_gesture_samples(session.gesture_name, candidates, threshold,
        coarse_threshold, calibration);
    spdlog::info("Stored gesture '{}' as id {} with {} representative samples", session.gesture_name, id,
                 candidates.size());
    return id;
  }

} // namespace signlang::signlang_det
