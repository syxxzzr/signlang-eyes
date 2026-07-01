#include "gesture_management_service.hpp"

#include "common/fixed_string.hpp"
#include "feature_extractor.hpp"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace signlang::signlang_det {
  namespace {

    using DistanceMatrix = std::vector<std::vector<float>>;

    auto median(std::vector<float> values) -> float {
      if (values.empty()) {
        return 0.0F;
      }
      const auto mid = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2U);
      std::nth_element(values.begin(), mid, values.end());
      auto result = *mid;
      if (values.size() % 2U == 0U) {
        const auto lower = *std::max_element(values.begin(), mid);
        result = (result + lower) * 0.5F;
      }
      return result;
    }

    auto compute_pairwise_distances(SignlangModel& model, const std::vector<EncodedSequence>& samples)
        -> DistanceMatrix {
      const auto count = samples.size();
      auto distances = std::vector<std::vector<float>>(count, std::vector<float>(count, 0.0F));
      for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t j = i + 1U; j < count; ++j) {
          const auto distance = model.dtw_distance(samples[i], samples[j]);
          distances[i][j] = distance;
          distances[j][i] = distance;
        }
      }
      return distances;
    }

    auto filter_outlier_samples(const std::vector<EncodedSequence>& samples, const DistanceMatrix& distances)
        -> std::vector<EncodedSequence> {
      if (samples.size() < 4U) {
        return samples;
      }

      auto mean_distances = std::vector<float>{};
      mean_distances.reserve(samples.size());
      for (std::size_t i = 0; i < samples.size(); ++i) {
        auto sum = 0.0F;
        auto finite_count = std::uint32_t{0};
        for (std::size_t j = 0; j < samples.size(); ++j) {
          if (i == j || !std::isfinite(distances[i][j])) {
            continue;
          }
          sum += distances[i][j];
          ++finite_count;
        }
        mean_distances.push_back(finite_count == 0 ? std::numeric_limits<float>::infinity()
                                                   : sum / static_cast<float>(finite_count));
      }

      const auto center = median(mean_distances);
      auto deviations = std::vector<float>{};
      deviations.reserve(mean_distances.size());
      for (const auto value : mean_distances) {
        if (std::isfinite(value)) {
          deviations.push_back(std::abs(value - center));
        }
      }

      const auto mad = median(deviations);
      if (mad <= 1e-6F || !std::isfinite(center)) {
        return samples;
      }

      const auto threshold = center + 3.0F * 1.4826F * mad;
      auto filtered = std::vector<EncodedSequence>{};
      filtered.reserve(samples.size());
      for (std::size_t i = 0; i < samples.size(); ++i) {
        if (mean_distances[i] <= threshold) {
          filtered.push_back(samples[i]);
        }
      }

      if (filtered.empty()) {
        return samples;
      }
      if (filtered.size() != samples.size()) {
        spdlog::info("Filtered {} outlier gesture prototype samples", samples.size() - filtered.size());
      }
      return filtered;
    }

    auto select_representative_samples(const std::vector<EncodedSequence>& samples, const DistanceMatrix& distances,
                                       std::size_t representative_count) -> std::vector<EncodedSequence> {
      if (samples.size() <= representative_count) {
        return samples;
      }

      const auto count = samples.size();

      auto cluster_cost = [&](const std::vector<std::size_t>& medoids) {
        auto cost = 0.0F;
        for (std::size_t sample_index = 0; sample_index < count; ++sample_index) {
          auto nearest = std::numeric_limits<float>::infinity();
          for (const auto medoid : medoids) {
            nearest = std::min(nearest, distances[sample_index][medoid]);
          }
          cost += nearest;
        }
        return cost;
      };

      auto medoids = std::vector<std::size_t>{};
      while (medoids.size() < representative_count) {
        auto best_index = std::size_t{0};
        auto best_cost = std::numeric_limits<float>::infinity();
        for (std::size_t candidate = 0; candidate < count; ++candidate) {
          if (std::find(medoids.begin(), medoids.end(), candidate) != medoids.end()) {
            continue;
          }
          auto trial = medoids;
          trial.push_back(candidate);
          const auto cost = cluster_cost(trial);
          if (cost < best_cost) {
            best_cost = cost;
            best_index = candidate;
          }
        }
        medoids.push_back(best_index);
      }

      for (auto iteration = 0; iteration < 4; ++iteration) {
        auto clusters = std::vector<std::vector<std::size_t>>(medoids.size());
        for (std::size_t sample_index = 0; sample_index < count; ++sample_index) {
          auto best_cluster = std::size_t{0};
          auto best_distance = std::numeric_limits<float>::infinity();
          for (std::size_t medoid_index = 0; medoid_index < medoids.size(); ++medoid_index) {
            const auto distance = distances[sample_index][medoids[medoid_index]];
            if (distance < best_distance) {
              best_distance = distance;
              best_cluster = medoid_index;
            }
          }
          clusters[best_cluster].push_back(sample_index);
        }

        auto changed = false;
        for (std::size_t cluster_index = 0; cluster_index < clusters.size(); ++cluster_index) {
          const auto& cluster = clusters[cluster_index];
          if (cluster.empty()) {
            continue;
          }

          auto best_member = medoids[cluster_index];
          auto best_cost = std::numeric_limits<float>::infinity();
          for (const auto candidate : cluster) {
            auto cost = 0.0F;
            for (const auto member : cluster) {
              cost += distances[candidate][member];
            }
            if (cost < best_cost) {
              best_cost = cost;
              best_member = candidate;
            }
          }

          if (best_member != medoids[cluster_index]) {
            medoids[cluster_index] = best_member;
            changed = true;
          }
        }

        if (!changed) {
          break;
        }
      }

      std::sort(medoids.begin(), medoids.end());
      auto representatives = std::vector<EncodedSequence>{};
      representatives.reserve(medoids.size());
      for (const auto medoid : medoids) {
        representatives.push_back(samples[medoid]);
      }
      return representatives;
    }

  } // namespace

  GestureManagementService::GestureManagementService(const ProgramOptions& options, SignlangModel& model,
                                                     std::mutex& model_mutex) :
      options_{options}, model_{model}, model_mutex_{model_mutex},
      database_{options.prototypes_path, model.embedding_dim()} {
    database_.ensure_valid_empty_or_existing();
  }

  auto GestureManagementService::handle_request(const GestureManagementRequest& request) -> GestureManagementResponse {
    try {
      switch (request.command) {
      case GestureManagementCommand::GetStatus:
        return handle_get_status(request);
      case GestureManagementCommand::ListGestures:
        return handle_list_gestures(request);
      case GestureManagementCommand::AddGestureBegin:
        return handle_add_begin(request);
      case GestureManagementCommand::AddGestureChunk:
        return handle_add_chunk(request);
      case GestureManagementCommand::AddGestureCommit:
        return handle_add_commit(request);
      case GestureManagementCommand::AddGestureAbort:
        return handle_add_abort(request);
      case GestureManagementCommand::DeleteGestureById:
      case GestureManagementCommand::DeleteGestureByName:
        return handle_delete_gesture(request);
      default:
        return make_response(request, GestureManagementStatus::UnsupportedCommand, "unsupported command");
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

  auto GestureManagementService::handle_get_status(const GestureManagementRequest& request)
      -> GestureManagementResponse {
    return make_response(request, GestureManagementStatus::Ok, "ok");
  }

  auto GestureManagementService::handle_list_gestures(const GestureManagementRequest& request)
      -> GestureManagementResponse {
    auto response = make_response(request, GestureManagementStatus::Ok);
    const auto gestures = database_.list_gestures();
    response.gesture_count =
        static_cast<std::uint32_t>(std::min<std::size_t>(gestures.size(), kGestureManagementMaxGestures));
    for (std::size_t index = 0; index < response.gesture_count; ++index) {
      response.gestures[index].id = gestures[index].id;
      response.gestures[index].sample_count = gestures[index].sample_count;
      response.gestures[index].enabled = gestures[index].enabled;
      common::copy_fixed_string(gestures[index].name, response.gestures[index].name);
    }
    if (gestures.size() > kGestureManagementMaxGestures) {
      common::copy_fixed_string("gesture list truncated", response.message);
    }
    return response;
  }

  auto GestureManagementService::handle_delete_gesture(const GestureManagementRequest& request)
      -> GestureManagementResponse {
    const auto deleted = request.command == GestureManagementCommand::DeleteGestureById
        ? database_.delete_gesture(request.gesture_id)
        : database_.delete_gesture(common::fixed_string_to_string(request.gesture_name));
    if (!deleted) {
      return make_response(request, GestureManagementStatus::NotFound, "gesture not found");
    }

    reload_model_prototypes();
    return make_response(request, GestureManagementStatus::Ok);
  }

  auto GestureManagementService::handle_add_begin(const GestureManagementRequest& request)
      -> GestureManagementResponse {
    const auto gesture_name = common::fixed_string_to_string(request.gesture_name);
    if (gesture_name.empty()) {
      throw std::runtime_error("gesture name must not be empty");
    }
    if (request.frame_count == 0) {
      throw std::runtime_error("upload frame count must be greater than zero");
    }

    upload_ = UploadSession{
        request.transfer_id,
        gesture_name,
        request.replace_existing,
        request.frame_count,
        std::vector<handpose_det::HandPoseFrameMetadata>(request.frame_count),
        std::vector<std::array<handpose_det::HandPoseDetection, kMaxHandCount>>(request.frame_count),
        std::vector<std::uint32_t>(request.frame_count, 0),
        std::vector<std::uint8_t>(request.frame_count, 0)};
    return make_response(request, GestureManagementStatus::Ok);
  }

  auto GestureManagementService::handle_add_chunk(const GestureManagementRequest& request)
      -> GestureManagementResponse {
    if (!upload_.has_value()) {
      throw std::runtime_error("no upload session is active");
    }
    if (request.transfer_id != upload_->transfer_id) {
      throw std::runtime_error("upload transfer id mismatch");
    }
    if (request.frame_index >= upload_->frame_count) {
      throw std::runtime_error("upload frame index exceeds declared frame count");
    }
    if (request.detection_count > kMaxHandCount) {
      throw std::runtime_error("upload frame detection count exceeds supported hand count");
    }

    upload_->metadata[request.frame_index] = request.frame_metadata;
    upload_->detections[request.frame_index] = request.detections;
    upload_->detection_counts[request.frame_index] = request.detection_count;
    upload_->received[request.frame_index] = 1;
    return make_response(request, GestureManagementStatus::Ok);
  }

  auto GestureManagementService::handle_add_commit(const GestureManagementRequest& request)
      -> GestureManagementResponse {
    if (!upload_.has_value()) {
      throw std::runtime_error("no upload session is active");
    }
    if (request.transfer_id != upload_->transfer_id) {
      throw std::runtime_error("upload transfer id mismatch");
    }
    if (!std::all_of(upload_->received.begin(), upload_->received.end(),
                     [](std::uint8_t received) { return received != 0; })) {
      throw std::runtime_error("upload has missing chunks");
    }

    const auto gesture_id = encode_uploaded_gesture(*upload_);
    upload_.reset();
    reload_model_prototypes();

    auto response = make_response(request, GestureManagementStatus::Ok);
    response.gesture_id = gesture_id;
    return response;
  }

  auto GestureManagementService::handle_add_abort(const GestureManagementRequest& request)
      -> GestureManagementResponse {
    upload_.reset();
    return make_response(request, GestureManagementStatus::Ok);
  }

  void GestureManagementService::reload_model_prototypes() {
    const std::lock_guard lock{model_mutex_};
    model_.reload_prototypes(options_.prototypes_path);
  }

  auto GestureManagementService::encode_uploaded_gesture(const UploadSession& session) -> std::uint32_t {
    auto extractor = FeatureExtractor{options_.min_keypoint_confidence};
    auto features = std::vector<FeatureVector>{};
    features.reserve(session.frame_count);
    for (std::uint32_t i = 0; i < session.frame_count; ++i) {
      if (auto feature =
              extractor.extract(session.metadata[i], session.detections[i].data(), session.detection_counts[i])) {
        features.push_back(*feature);
      }
    }

    const auto sequence_length = model_.expected_sequence_length();
    if (features.size() < sequence_length) {
      throw std::runtime_error("uploaded gesture does not contain enough valid handpose frames");
    }

    const auto hop = std::max<std::uint32_t>(
        1, static_cast<std::uint32_t>(static_cast<float>(sequence_length) * (1.0F - options_.upload_window_overlap)));
    auto uploaded_samples = std::vector<EncodedSequence>{};
    {
      const std::lock_guard lock{model_mutex_};
      for (std::size_t start = 0; start + sequence_length <= features.size(); start += hop) {
        auto window =
            std::vector<FeatureVector>{features.begin() + static_cast<std::ptrdiff_t>(start),
                                       features.begin() + static_cast<std::ptrdiff_t>(start + sequence_length)};
        uploaded_samples.push_back(model_.encode_features(window));
      }
      const auto distances = compute_pairwise_distances(model_, uploaded_samples);
      uploaded_samples = filter_outlier_samples(uploaded_samples, distances);
    }

    auto candidate_samples = session.replace_existing ? std::vector<EncodedSequence>{}
                                                      : database_.load_gesture_samples(session.gesture_name);
    const auto existing_sample_count = candidate_samples.size();
    candidate_samples.insert(candidate_samples.end(), uploaded_samples.begin(), uploaded_samples.end());

    std::vector<EncodedSequence> representative_samples;
    {
      const std::lock_guard lock{model_mutex_};
      const auto distances = compute_pairwise_distances(model_, candidate_samples);
      const auto representative_count =
          std::min<std::size_t>(candidate_samples.size(), options_.max_representative_samples);
      representative_samples = select_representative_samples(candidate_samples, distances, representative_count);
    }
    const auto gesture_id = database_.replace_gesture_samples(session.gesture_name, representative_samples);
    spdlog::info(
        "Stored uploaded gesture '{}' as id {}; uploaded_windows={}, existing_samples={}, stored_representatives={}",
        session.gesture_name, gesture_id, uploaded_samples.size(), existing_sample_count,
        representative_samples.size());
    return gesture_id;
  }

} // namespace signlang::signlang_det
