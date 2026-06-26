#include "common/logging.hpp"
#include "feature_extractor.hpp"
#include "iceoryx_gateway.hpp"
#include "keypoint_ring_buffer.hpp"
#include "program_options.hpp"
#include "signlang_model.hpp"
#include "signlang_result.hpp"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>

namespace {

  volatile std::sig_atomic_t g_should_stop = 0;

  void handle_shutdown_signal(int) { g_should_stop = 1; }

  void install_signal_handlers() {
    std::signal(SIGINT, handle_shutdown_signal);
    std::signal(SIGTERM, handle_shutdown_signal);
  }

  auto build_result(const std::vector<signlang::signlang_det::FeatureVector>& window,
                    const signlang::signlang_det::SignlangModel::InferenceResult& inference,
                    const signlang::signlang_det::ProgramOptions& options,
                    const signlang::signlang_det::SignlangModel& model, bool recognized)
      -> signlang::signlang_det::SignlangResult {
    using signlang::signlang_det::copy_string;
    using signlang::signlang_det::SignlangResult;
    using signlang::signlang_det::steady_timestamp_ns;

    auto result = SignlangResult{};
    result.timestamp_ns = steady_timestamp_ns();
    result.window_start_sequence = window.front().source_sequence_number;
    result.window_end_sequence = window.back().source_sequence_number;
    result.sequence_length = options.sequence_length;
    result.overlap_ratio = options.overlap_ratio;
    result.inference_time_ms = inference.inference_time_ms;
    result.recognized = recognized;
    result.gesture_id = inference.gesture_id;
    result.confidence = inference.confidence;
    result.second_confidence = inference.second_confidence;
    result.confidence_margin = inference.confidence - inference.second_confidence;
    result.distance = inference.distance;

    const auto* gesture_name = model.get_gesture_name(inference.gesture_id);
    copy_string(gesture_name, result.gesture_name);

    const auto candidate_count =
        std::min<std::size_t>(inference.candidates.size(), signlang::signlang_det::kMaxGestureCandidates);
    result.candidate_count = static_cast<std::uint32_t>(candidate_count);
    for (std::size_t index = 0; index < candidate_count; ++index) {
      const auto& candidate = inference.candidates[index];
      auto& output_candidate = result.candidates[index];
      output_candidate.gesture_id = candidate.gesture_id;
      output_candidate.confidence = candidate.confidence;
      output_candidate.distance = candidate.distance;
      copy_string(model.get_gesture_name(candidate.gesture_id), output_candidate.gesture_name);
    }

    return result;
  }

  void receiver_loop(const signlang::signlang_det::ProgramOptions& options,
                     signlang::signlang_det::KeypointRingBuffer& ring_buffer, const std::atomic<bool>& should_stop) {
    using signlang::signlang_det::FeatureExtractor;
    using signlang::signlang_det::IpcHandposeSubscriber;

    auto subscriber = IpcHandposeSubscriber{options.input_service_name, options.subscriber_buffer_size};
    auto extractor = FeatureExtractor{options.min_keypoint_confidence};

    while (!should_stop && subscriber.wait_for_work()) {
      if (!should_stop) {
        subscriber.receive_latest([&](const auto& metadata, const auto* detections, auto count) {
          if (auto feature = extractor.extract(metadata, detections, count)) {
            ring_buffer.push(*feature);
          }
        });
      }
    }
  }

  void inference_loop(const signlang::signlang_det::ProgramOptions& options,
                      signlang::signlang_det::KeypointRingBuffer& ring_buffer, const std::atomic<bool>& should_stop) {
    using signlang::signlang_det::IpcPrototypeControlServer;
    using signlang::signlang_det::IpcSignlangDetStateMonitor;
    using signlang::signlang_det::IpcSignlangPublisher;
    using signlang::signlang_det::PrototypeControlCommand;
    using signlang::signlang_det::PrototypeControlResponse;
    using signlang::signlang_det::PrototypeControlStatus;
    using signlang::signlang_det::SignlangModel;

    auto model = SignlangModel{options.model_path, options.prototypes_path, options.npu_core_mask,
                               options.motion_weight, options.dtw_window_ratio};
    if (model.expected_sequence_length() != options.sequence_length) {
      throw std::runtime_error("Configured sequence length " + std::to_string(options.sequence_length) +
                               " does not match model sequence length " +
                               std::to_string(model.expected_sequence_length()));
    }
    spdlog::info("Sign language model loaded successfully");

    auto publisher = IpcSignlangPublisher{options.output_service_name};
    auto prototype_control_server = std::optional<IpcPrototypeControlServer>{};
    if (options.prototype_control_service_name.has_value()) {
      prototype_control_server.emplace(options.prototype_control_service_name.value());
    }
    auto control_response = [&](const auto& request) {
      auto response = PrototypeControlResponse{};
      response.status = PrototypeControlStatus::Ok;
      response.request_id = request.request_id;
      response.loaded_gesture_count = static_cast<std::uint32_t>(model.loaded_gesture_count());
      response.loaded_sample_count = static_cast<std::uint32_t>(model.loaded_sample_count());
      auto copy_message = [&](const std::string& message) {
        std::fill(response.message.begin(), response.message.end(), '\0');
        const auto count = std::min(message.size(), response.message.size() - 1);
        std::copy_n(message.data(), count, response.message.data());
      };

      try {
        if (request.command == PrototypeControlCommand::ReloadPrototypes) {
          model.reload_prototypes(options.prototypes_path);
          response.loaded_gesture_count = static_cast<std::uint32_t>(model.loaded_gesture_count());
          response.loaded_sample_count = static_cast<std::uint32_t>(model.loaded_sample_count());
          copy_message("reloaded");
        } else if (request.command == PrototypeControlCommand::GetStatus) {
          copy_message("ok");
        } else {
          response.status = PrototypeControlStatus::UnsupportedCommand;
          copy_message("unsupported command");
        }
      } catch (const std::exception& error) {
        response.status = PrototypeControlStatus::Failed;
        copy_message(error.what());
      }

      return response;
    };
    auto state_monitor = std::optional<IpcSignlangDetStateMonitor>{};
    if (options.state_event_service_name.has_value() && options.state_blackboard_service_name.has_value()) {
      state_monitor.emplace(options.state_event_service_name.value(), options.state_blackboard_service_name.value());
    }
    auto gate_enabled = [&]() { return !state_monitor.has_value() || state_monitor->is_enabled(); };
    auto poll_gate = [&]() {
      if (state_monitor.has_value()) {
        state_monitor->try_wait_for_state_change();
      }
    };

    const auto hop_frames = std::max<std::uint32_t>(
        1, static_cast<std::uint32_t>(options.sequence_length * (1.0f - options.overlap_ratio)));
    auto next_window_end_seq = std::uint64_t{hop_frames};

    while (!should_stop) {
      // Check for state changes before waiting for buffer
      poll_gate();
      if (prototype_control_server.has_value()) {
        prototype_control_server->process_pending_requests(control_response);
      }

      if (!gate_enabled()) {
        // Poll for state change with stop check to avoid hang on shutdown
        while (!should_stop.load() && !gate_enabled()) {
          poll_gate();
          if (prototype_control_server.has_value()) {
            prototype_control_server->process_pending_requests(control_response);
          }
          if (!gate_enabled()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
          }
        }
        if (should_stop.load()) {
          break;
        }
        // Discard stale data accumulated during disabled period
        ring_buffer.clear();
        next_window_end_seq = hop_frames;
        continue;
      }

      auto window = ring_buffer.wait_for_window(options.sequence_length, next_window_end_seq, should_stop);
      if (!window.has_value()) {
        break;
      }

      const auto window_end_seq = window->back().source_sequence_number;
      try {
        const auto inference_result = model.infer(*window);

        auto recognized = inference_result.recognized && inference_result.confidence >= options.confidence_threshold;
        const auto margin = inference_result.confidence - inference_result.second_confidence;
        if (margin < options.confidence_margin) {
          recognized = false;
        }

        const auto result = build_result(*window, inference_result, options, model, recognized);
        if (recognized) {
          spdlog::info("Sign language detected: {} (confidence: {:.2f}, margin: {:.2f})",
                       model.get_gesture_name(inference_result.gesture_id), inference_result.confidence, margin);
        }
        publisher.publish(result);

        next_window_end_seq = window_end_seq + hop_frames;
      } catch (const std::exception& e) {
        spdlog::error("Inference error: {}", e.what());
        next_window_end_seq = window_end_seq + hop_frames;
      }
    }
  }

} // namespace

auto main(int argc, char** argv) -> int {
  using signlang::signlang_det::compute_buffer_capacity;
  using signlang::signlang_det::KeypointRingBuffer;
  using signlang::signlang_det::parse_program_options;
  using signlang::signlang_det::ProgramOptions;
  using signlang::signlang_det::ProgramUsage;

  signlang::logging::initialize();

  try {
    const auto parse_result = parse_program_options(argc, argv);
    if (const auto* usage = std::get_if<ProgramUsage>(&parse_result); usage != nullptr) {
      std::cout << usage->text << '\n';
      return 0;
    }

    const auto options = std::get<ProgramOptions>(parse_result);
    signlang::logging::initialize(options.logging);
    install_signal_handlers();

    spdlog::info("Starting sign language detector");
    spdlog::info("Model: {}", options.model_path);
    spdlog::info("Sequence length: {}, overlap ratio: {:.1f}%", options.sequence_length, options.overlap_ratio * 100);

    const auto buffer_capacity = compute_buffer_capacity(options.sequence_length, options.overlap_ratio);
    auto ring_buffer = KeypointRingBuffer{buffer_capacity};
    auto should_stop = std::atomic<bool>{false};
    auto worker_error = std::exception_ptr{nullptr};
    auto worker_error_mutex = std::mutex{};

    auto record_worker_error = [&](std::exception_ptr error) {
      {
        const std::lock_guard lock{worker_error_mutex};
        if (worker_error == nullptr) {
          worker_error = error;
        }
      }
      should_stop.store(true);
      ring_buffer.notify_stop();
    };

    auto receiver_thread = std::thread{[&]() {
      try {
        receiver_loop(options, ring_buffer, should_stop);
      } catch (...) {
        record_worker_error(std::current_exception());
      }
    }};

    auto inference_thread = std::thread{[&]() {
      try {
        inference_loop(options, ring_buffer, should_stop);
      } catch (...) {
        record_worker_error(std::current_exception());
      }
    }};

    while (g_should_stop == 0 && !should_stop.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    should_stop.store(true);
    ring_buffer.notify_stop();

    receiver_thread.join();
    inference_thread.join();

    if (worker_error != nullptr) {
      std::rethrow_exception(worker_error);
    }

    return 0;
  } catch (const std::exception& e) {
    spdlog::error("Error: {}", e.what());
    return 1;
  }
}
