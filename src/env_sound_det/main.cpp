#include "common/audio_ring_buffer.hpp"
#include "common/runtime.hpp"
#include "iceoryx_gateway.hpp"
#include "program_options.hpp"
#include "spdlog/spdlog.h"
#include "yamnet_model.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace {

  auto is_dangerous_sound_label(const std::array<char, signlang::env_sound_det::kMaxClassLabelLength>& label) -> bool {
    const std::string_view label_view{label.data()};
    return label_view == "Air horn, truck horn" || label_view == "Vehicle horn, car horn, honking" ||
        label_view == "Train horn";
  }

  auto has_dangerous_sound(const signlang::env_sound_det::YamnetInferenceResult& inference_result) -> bool {
    for (std::uint32_t index = 0; index < inference_result.detected_class_count; ++index) {
      if (is_dangerous_sound_label(inference_result.detected_classes[index].label)) {
        return true;
      }
    }

    return false;
  }

  auto ring_capacity_samples(std::uint64_t window_sample_count, std::uint64_t hop_sample_count) -> std::uint64_t {
    const auto minimum_capacity = window_sample_count + std::max(window_sample_count, hop_sample_count);
    const auto one_second = static_cast<std::uint64_t>(signlang::env_sound_det::kYamnetSampleRateHz);
    return std::max(minimum_capacity, window_sample_count + one_second);
  }

} // namespace

auto main(int argc, char** argv) -> int {
  using signlang::common::AudioRingBuffer;
  using signlang::common::AudioWindow;
  using signlang::common::hop_samples_for_overlap;
  using signlang::common::samples_for_window_ms;
  using signlang::env_sound_det::IpcAudioSubscriber;
  using signlang::env_sound_det::IpcStateControlClient;
  using signlang::env_sound_det::kYamnetSampleRateHz;
  using signlang::env_sound_det::parse_program_options;
  using signlang::env_sound_det::YamnetModel;

  return signlang::runtime::run_module(argc, argv, parse_program_options, [&](const auto& options) {
    spdlog::info("Starting environment sound detector");
    spdlog::info("Model: {}", options.model_path);
    spdlog::info("Window: {}ms, overlap: {:.1f}%", options.window_ms, options.overlap_ratio * 100);

    const auto window_sample_count = samples_for_window_ms(kYamnetSampleRateHz, options.window_ms);
    const auto hop_sample_count = hop_samples_for_overlap(window_sample_count, options.overlap_ratio);
    if (window_sample_count == 0) {
      throw std::runtime_error("Detection window has no samples");
    }

    AudioRingBuffer audio_buffer{ring_capacity_samples(window_sample_count, hop_sample_count), kYamnetSampleRateHz};
    std::atomic_bool should_stop{false};
    std::atomic_bool downstream_active{false};
    std::exception_ptr worker_error = nullptr;
    std::mutex worker_error_mutex;

    auto record_worker_error = [&](std::exception_ptr error) {
      {
        const std::lock_guard<std::mutex> lock{worker_error_mutex};
        if (worker_error == nullptr) {
          worker_error = error;
        }
      }
      should_stop.store(true);
      audio_buffer.notify_stop();
    };

    std::thread receiver_thread{[&] {
      try {
        auto audio_subscriber = std::optional<IpcAudioSubscriber>{};

        while (!should_stop.load()) {
          if (!downstream_active.load()) {
            audio_subscriber.reset();
            audio_buffer.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
          }

          if (!audio_subscriber.has_value()) {
            audio_subscriber.emplace(options.audio_service_name, options.subscriber_buffer_size);
          }

          if (audio_subscriber->wait_for_work() && !should_stop.load() && downstream_active.load()) {
            audio_subscriber->receive_available(audio_buffer);
          }
        }
      } catch (...) {
        record_worker_error(std::current_exception());
      }
    }};

    std::thread detector_thread{[&] {
      try {
        spdlog::info("Initializing YAMNet model");
        YamnetModel model{options.model_path, options.class_map_path, options.npu_core_mask, options.rknn_priority_flag,
                          options.score_threshold};
        spdlog::info("YAMNet model loaded successfully");

        IpcStateControlClient state_control_client{options.state_control_service_name};
        AudioWindow audio_window;
        std::optional<std::uint64_t> next_window_start_sample;

        while (!should_stop.load()) {
          const auto has_downstream = state_control_client.has_server();
          downstream_active.store(has_downstream);
          if (!has_downstream) {
            audio_buffer.clear();
            next_window_start_sample = std::nullopt;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
          }

          if (!audio_buffer.wait_for_window(next_window_start_sample, window_sample_count, hop_sample_count,
                                            should_stop, audio_window)) {
            break;
          }

          const auto inference_result = model.infer(audio_window);

          if (has_dangerous_sound(inference_result)) {
            spdlog::warn("Dangerous sound detected!");
            state_control_client.enter_dangerous_sound_state();
          }
          next_window_start_sample = audio_window.start_sample_index + hop_sample_count;
        }
      } catch (...) {
        record_worker_error(std::current_exception());
      }
    }};

    while (!should_stop.load() && !signlang::runtime::shutdown_requested()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    should_stop.store(true);
    audio_buffer.notify_stop();

    receiver_thread.join();
    detector_thread.join();

    if (worker_error != nullptr) {
      std::rethrow_exception(worker_error);
    }

    return 0;
  });
}
