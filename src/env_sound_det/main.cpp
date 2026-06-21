#include "common/audio_ring_buffer.hpp"
#include "common/logging.hpp"
#include "iceoryx_gateway.hpp"
#include "program_options.hpp"
#include "spdlog/spdlog.h"
#include "yamnet_model.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <variant>

namespace {

  volatile std::sig_atomic_t g_should_stop = 0;

  void handle_shutdown_signal(int /* signal_number */) { g_should_stop = 1; }

  void install_signal_handlers() {
    std::signal(SIGINT, handle_shutdown_signal);
    std::signal(SIGTERM, handle_shutdown_signal);
  }

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
  using signlang::env_sound_det::ProgramOptions;
  using signlang::env_sound_det::ProgramUsage;
  using signlang::env_sound_det::YamnetModel;

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
        IpcAudioSubscriber audio_subscriber{options.audio_service_name, options.subscriber_buffer_size};

        while (!should_stop.load() && audio_subscriber.wait_for_work()) {
          if (!should_stop.load()) {
            audio_subscriber.receive_available(audio_buffer);
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

        while (audio_buffer.wait_for_window(next_window_start_sample, window_sample_count, hop_sample_count,
                                            should_stop, audio_window)) {
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

    while (!should_stop.load() && g_should_stop == 0) {
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
  } catch (const std::exception& error) {
    spdlog::error("{}", error.what());
    return 1;
  }
}
