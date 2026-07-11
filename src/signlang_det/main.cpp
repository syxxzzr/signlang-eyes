#include "common/fixed_string.hpp"
#include "common/runtime.hpp"
#include "common/time.hpp"
#include "feature_extractor.hpp"
#include "gesture_management_service.hpp"
#include "gesture_pipeline.hpp"
#include "iceoryx_gateway.hpp"
#include "program_options.hpp"
#include "signlang_model.hpp"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace {

  class SegmentQueue {
  public:
    explicit SegmentQueue(std::size_t capacity) : capacity_{capacity} {
      if (capacity_ == 0) throw std::invalid_argument("segment queue capacity must be greater than zero");
    }

    auto push(signlang::signlang_det::GestureSegment segment) -> bool {
      auto dropped = false;
      {
        const std::lock_guard lock{mutex_};
        if (queue_.size() == capacity_) {
          queue_.pop_front();
          dropped = true;
        }
        queue_.push_back(std::move(segment));
      }
      changed_.notify_one();
      return dropped;
    }

    auto wait_pop(const std::atomic_bool& stop) -> std::optional<signlang::signlang_det::GestureSegment> {
      auto lock = std::unique_lock{mutex_};
      changed_.wait_for(lock, std::chrono::milliseconds{5}, [&] { return stop.load() || !queue_.empty(); });
      if (stop.load() || queue_.empty()) return std::nullopt;
      auto segment = std::move(queue_.front());
      queue_.pop_front();
      return segment;
    }

    void clear() {
      const std::lock_guard lock{mutex_};
      queue_.clear();
    }

    void notify_stop() { changed_.notify_all(); }

  private:
    std::size_t capacity_;
    std::deque<signlang::signlang_det::GestureSegment> queue_;
    std::mutex mutex_;
    std::condition_variable changed_;
  };

  auto build_result(const signlang::signlang_det::SignlangModel::InferenceResult& inference,
                    const signlang::signlang_det::SignlangModel& model)
      -> signlang::signlang_det::SignlangResult {
    using namespace signlang::signlang_det;
    auto result = SignlangResult{};
    const auto& match = inference.match;
    const auto& segment = inference.segment;
    result.timestamp_ns = signlang::common::steady_timestamp_ns();
    result.window_start_sequence = segment.start_sequence;
    result.window_end_sequence = segment.end_sequence;
    result.sequence_length = segment.valid_length;
    result.overlap_ratio = 0.0F;
    result.inference_time_ms = inference.inference_time_ms;
    result.recognized = match.recognized;
    result.status = match.recognized ? RecognitionStatus::Recognized : RecognitionStatus::Rejected;
    result.rejection_reason = match.rejection_reason;
    result.gesture_id = match.gesture_id;
    result.confidence = match.candidates.empty() ? 0.0F : match.candidates.front().confidence;
    result.second_confidence = match.candidates.size() > 1U ? match.candidates[1].confidence : 0.0F;
    result.confidence_margin = result.confidence - result.second_confidence;
    result.distance = match.top1_dtw_distance;
    result.segment_start_timestamp_ns = segment.start_timestamp_ns;
    result.segment_end_timestamp_ns = segment.end_timestamp_ns;
    result.original_length = segment.original_length;
    result.valid_length = segment.valid_length;
    result.segment_quality = segment.quality.score;
    result.coarse_distance = match.coarse_distance;
    result.top1_dtw_distance = match.top1_dtw_distance;
    result.top2_dtw_distance = match.top2_dtw_distance;
    result.distance_margin = match.distance_margin;
    result.applied_dtw_threshold = match.applied_dtw_threshold;
    result.applied_coarse_threshold = match.applied_coarse_threshold;
    result.forced_max_length = segment.forced_max_length;
    signlang::common::copy_fixed_string(model.get_gesture_name(match.gesture_id), result.gesture_name);
    result.candidate_count = static_cast<std::uint32_t>(
        std::min<std::size_t>(match.candidates.size(), kMaxGestureCandidates));
    for (std::size_t index = 0; index < result.candidate_count; ++index) {
      const auto& source = match.candidates[index];
      auto& destination = result.candidates[index];
      destination.gesture_id = source.gesture_id;
      destination.confidence = source.confidence;
      destination.distance = source.dtw_distance;
      signlang::common::copy_fixed_string(model.get_gesture_name(source.gesture_id), destination.gesture_name);
    }
    return result;
  }

  auto build_quality_rejection(const signlang::signlang_det::GestureSegment& segment)
      -> signlang::signlang_det::SignlangResult {
    using namespace signlang::signlang_det;
    auto result = SignlangResult{};
    result.timestamp_ns = signlang::common::steady_timestamp_ns();
    result.status = RecognitionStatus::Rejected;
    result.rejection_reason = RejectionReason::SegmentQuality;
    result.recognized = false;
    result.original_length = static_cast<std::uint32_t>(segment.frames.size());
    result.sequence_length = std::min(result.original_length, kMaxSequenceLength);
    result.valid_length = result.sequence_length;
    result.overlap_ratio = 0.0F;
    result.segment_quality = segment.quality.score;
    result.forced_max_length = segment.forced_max_length;
    if (!segment.frames.empty()) {
      result.window_start_sequence = segment.frames.front().source_sequence_number;
      result.window_end_sequence = segment.frames.back().source_sequence_number;
      result.segment_start_timestamp_ns = segment.frames.front().timestamp_ns;
      result.segment_end_timestamp_ns = segment.frames.back().timestamp_ns;
    }
    return result;
  }

  void receiver_loop(const signlang::signlang_det::ProgramOptions& options,
                     SegmentQueue& queue, const std::atomic_bool& stop,
                     const std::atomic_bool& active) {
    using namespace signlang::signlang_det;
    auto subscriber = std::optional<IpcHandposeSubscriber>{};
    auto extractor = FeatureExtractor{options.min_keypoint_confidence};
    auto segmenter = GestureSegmenter{options.segmenter};
    while (!stop.load()) {
      if (!active.load()) {
        subscriber.reset();
        extractor.reset();
        segmenter.reset();
        queue.clear();
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
        continue;
      }
      if (!subscriber) subscriber.emplace(options.input_service_name, options.subscriber_buffer_size);
      if (!subscriber->wait_for_work()) continue;
      subscriber->receive_all([&](const auto& metadata, const auto* detections, auto count) {
        const auto frame = extractor.extract(metadata, detections, count);
        auto update = segmenter.push(frame);
        if (update.event == SegmentEvent::DroppedSequenceGap) {
          spdlog::warn("Dropped active gesture after handpose sequence gap at {}", metadata.source_sequence_number);
        } else if (update.event == SegmentEvent::DroppedTooShort) {
          spdlog::debug("Dropped short gesture segment");
        } else if (update.segment) {
          const auto forced = update.segment->forced_max_length;
          const auto length = update.segment->frames.size();
          if (queue.push(std::move(*update.segment))) spdlog::warn("Segment queue full; dropped oldest segment");
          spdlog::debug("Queued gesture segment frames={} forced={}", length, forced);
        }
      });
    }
  }

  auto control_handler(const signlang::signlang_det::ProgramOptions& options,
                       signlang::signlang_det::SignlangModel& model, std::mutex& mutex,
                       const signlang::signlang_det::PrototypeControlRequest& request)
      -> signlang::signlang_det::PrototypeControlResponse {
    using namespace signlang::signlang_det;
    auto response = PrototypeControlResponse{};
    response.request_id = request.request_id;
    try {
      const std::lock_guard lock{mutex};
      if (request.command == PrototypeControlCommand::ReloadPrototypes) model.reload_prototypes(options.prototypes_path);
      else if (request.command != PrototypeControlCommand::GetStatus) {
        response.status = PrototypeControlStatus::UnsupportedCommand;
        signlang::common::copy_fixed_string("unsupported command", response.message);
        return response;
      }
      response.status = PrototypeControlStatus::Ok;
      response.loaded_gesture_count = static_cast<std::uint32_t>(model.loaded_gesture_count());
      response.loaded_sample_count = static_cast<std::uint32_t>(model.loaded_sample_count());
      signlang::common::copy_fixed_string("ok", response.message);
    } catch (const std::exception& error) {
      response.status = PrototypeControlStatus::Failed;
      signlang::common::copy_fixed_string(error.what(), response.message);
    }
    return response;
  }

  void inference_loop(const signlang::signlang_det::ProgramOptions& options,
                      SegmentQueue& queue, const std::atomic_bool& stop,
                      std::atomic_bool& active, signlang::signlang_det::SignlangModel& model, std::mutex& mutex) {
    using namespace signlang::signlang_det;
    auto publisher = IpcSignlangPublisher{options.output_service_name};
    auto control = std::optional<IpcPrototypeControlServer>{};
    if (options.prototype_control_service_name) control.emplace(*options.prototype_control_service_name);
    auto last_id = std::optional<std::uint32_t>{};
    auto last_time = std::chrono::steady_clock::time_point{};
    while (!stop.load()) {
      if (control) control->process_pending_requests(
          [&](const auto& request) { return control_handler(options, model, mutex, request); });
      if (!publisher.has_subscribers()) {
        active.store(false);
        queue.clear();
        last_id.reset();
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
        continue;
      }
      active.store(true);
      auto segment = queue.wait_pop(stop);
      if (!segment) continue;
      if (segment->quality.present_frame_ratio < options.preprocessing.minimum_present_frame_ratio ||
          segment->quality.mean_confidence < options.preprocessing.minimum_mean_confidence ||
          segment->quality.score < options.preprocessing.minimum_quality) {
        spdlog::debug("Rejected gesture segment for quality: ratio={} confidence={} score={}",
                      segment->quality.present_frame_ratio, segment->quality.mean_confidence, segment->quality.score);
        if (options.publish_rejections) publisher.publish(build_quality_rejection(*segment));
        continue;
      }
      try {
        auto inference = SignlangModel::InferenceResult{};
        auto result = SignlangResult{};
        {
          const std::lock_guard lock{mutex};
          inference = model.infer(*segment);
          result = build_result(inference, model);
        }
        if (!result.recognized && !options.publish_rejections) continue;
        if (result.recognized && options.duplicate_suppression_ms > 0 && last_id == result.gesture_id &&
            std::chrono::steady_clock::now() - last_time < std::chrono::milliseconds{options.duplicate_suppression_ms}) {
          continue;
        }
        if (result.recognized) {
          last_id = result.gesture_id;
          last_time = std::chrono::steady_clock::now();
        }
        publisher.publish(result);
        spdlog::info("Gesture segment recognized={} name={} dtw={} margin={} reason={}", result.recognized,
                     result.gesture_name.data(), result.top1_dtw_distance, result.distance_margin,
                     static_cast<std::uint32_t>(result.rejection_reason));
      } catch (const std::exception& error) {
        spdlog::error("Gesture segment inference failed: {}", error.what());
      }
    }
  }

  void management_loop(const signlang::signlang_det::ProgramOptions& options, const std::atomic_bool& stop,
                       signlang::signlang_det::SignlangModel& model, std::mutex& mutex) {
    using namespace signlang::signlang_det;
    if (!options.gesture_management_service_name) return;
    auto service = GestureManagementService{options, model, mutex};
    auto server = IpcGestureManagementServer{*options.gesture_management_service_name};
    while (!stop.load()) {
      server.process_pending_requests([&](const auto& request) { return service.handle_request(request); });
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
  }

} // namespace

auto main(int argc, char** argv) -> int {
  using namespace signlang::signlang_det;
  return signlang::runtime::run_module(argc, argv, parse_program_options, [&](const auto& options) {
    spdlog::info("Starting sign language detector with model {}", options.model_path);
    auto model = SignlangModel{options.model_path, options.prototypes_path, options.npu_core_mask,
                               options.preprocessing, options.matcher, options.min_keypoint_confidence};
    auto queue = SegmentQueue{options.segment_queue_capacity};
    auto stop = std::atomic_bool{false};
    auto active = std::atomic_bool{false};
    auto model_mutex = std::mutex{};
    auto worker_error = std::exception_ptr{};
    auto error_mutex = std::mutex{};
    const auto record_error = [&](std::exception_ptr error) {
      const std::lock_guard lock{error_mutex};
      if (!worker_error) worker_error = std::move(error);
      stop.store(true);
      queue.notify_stop();
    };
    auto receiver = std::thread{[&] { try { receiver_loop(options, queue, stop, active); } catch (...) { record_error(std::current_exception()); } }};
    auto inference = std::thread{[&] { try { inference_loop(options, queue, stop, active, model, model_mutex); } catch (...) { record_error(std::current_exception()); } }};
    auto management = std::thread{[&] { try { management_loop(options, stop, model, model_mutex); } catch (...) { record_error(std::current_exception()); } }};
    while (!signlang::runtime::shutdown_requested() && !stop.load()) std::this_thread::sleep_for(std::chrono::milliseconds{100});
    stop.store(true);
    queue.notify_stop();
    receiver.join(); inference.join(); management.join();
    if (worker_error) std::rethrow_exception(worker_error);
    return 0;
  });
}
