#include "audio_ring_buffer.hpp"
#include "env_sound_result.hpp"
#include "iceoryx_gateway.hpp"
#include "program_options.hpp"
#include "yamnet_model.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <variant>

namespace {

  volatile std::sig_atomic_t g_should_stop = 0;

  void handle_shutdown_signal(int /* signal_number */) { g_should_stop = 1; }

  void install_signal_handlers() {
    std::signal(SIGINT, handle_shutdown_signal);
    std::signal(SIGTERM, handle_shutdown_signal);
  }

  void copy_inference_result(const signlang::env_sound_det::YamnetInferenceResult& inference_result,
                             signlang::env_sound_det::EnvSoundDetectionResult& output_result) {
    output_result.model_input_sample_count = inference_result.model_input_sample_count;
    output_result.score_frame_count = inference_result.score_frame_count;
    output_result.inference_time_ms = inference_result.inference_time_ms;
    output_result.top_class_count = inference_result.top_class_count;

    for (std::uint32_t index = 0; index < output_result.top_class_count; ++index) {
      output_result.top_classes[index] = inference_result.top_classes[index];
    }
  }

  auto ring_capacity_samples(std::uint64_t window_sample_count, std::uint64_t hop_sample_count) -> std::uint64_t {
    const auto minimum_capacity = window_sample_count + std::max(window_sample_count, hop_sample_count);
    const auto one_second = static_cast<std::uint64_t>(signlang::env_sound_det::kYamnetSampleRateHz);
    return std::max(minimum_capacity, window_sample_count + one_second);
  }

} // namespace

auto main(int argc, char** argv) -> int {
  using signlang::env_sound_det::AudioRingBuffer;
  using signlang::env_sound_det::AudioWindow;
  using signlang::env_sound_det::EnvSoundDetectionResult;
  using signlang::env_sound_det::hop_samples_for_overlap;
  using signlang::env_sound_det::IpcAudioSubscriber;
  using signlang::env_sound_det::IpcResultPublisher;
  using signlang::env_sound_det::kYamnetSampleRateHz;
  using signlang::env_sound_det::parse_program_options;
  using signlang::env_sound_det::ProgramOptions;
  using signlang::env_sound_det::ProgramUsage;
  using signlang::env_sound_det::samples_for_window_ms;
  using signlang::env_sound_det::YamnetModel;

  try {
    const auto parse_result = parse_program_options(argc, argv);
    if (const auto* usage = std::get_if<ProgramUsage>(&parse_result); usage != nullptr) {
      std::cout << usage->text << '\n';
      return 0;
    }

    const auto options = std::get<ProgramOptions>(parse_result);
    install_signal_handlers();

    const auto window_sample_count = samples_for_window_ms(kYamnetSampleRateHz, options.window_ms);
    const auto hop_sample_count = hop_samples_for_overlap(window_sample_count, options.overlap_ratio);
    if (window_sample_count == 0) {
      throw std::runtime_error("Detection window has no samples");
    }

    AudioRingBuffer audio_buffer{ring_capacity_samples(window_sample_count, hop_sample_count)};
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
        const auto poll_period = std::chrono::milliseconds(options.poll_period_ms);

        while (!should_stop.load()) {
          const auto receive_stats = audio_subscriber.receive_available(audio_buffer);
          if (receive_stats.accepted_count == 0) {
            std::this_thread::sleep_for(poll_period);
          }
        }
      } catch (...) {
        record_worker_error(std::current_exception());
      }
    }};

    std::thread detector_thread{[&] {
      try {
        YamnetModel model{options.model_path, options.class_map_path, options.npu_core_mask, options.rknn_priority_flag,
                          options.top_k};
        IpcResultPublisher result_publisher{options.result_service_name};
        AudioWindow audio_window;
        std::optional<std::uint64_t> next_window_start_sample;
        std::uint64_t result_sequence_number = 0;

        while (audio_buffer.wait_for_window(next_window_start_sample, window_sample_count, hop_sample_count,
                                            should_stop, audio_window)) {
          const auto inference_result = model.infer(audio_window);

          EnvSoundDetectionResult result{};
          result.sequence_number = result_sequence_number++;
          result.timestamp_ns = static_cast<std::uint64_t>(
              std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
                  .count());
          result.audio_start_sample_index = audio_window.start_sample_index;
          result.audio_end_sample_index = audio_window.end_sample_index;
          result.latest_audio_sequence_number = audio_window.latest_audio_sequence_number;
          result.latest_audio_timestamp_ns = audio_window.latest_audio_timestamp_ns;
          result.latest_audio_sample_rate_hz = audio_window.latest_audio_sample_rate_hz;
          result.latest_audio_publish_period_ms = audio_window.latest_audio_publish_period_ms;
          result.latest_audio_frame_count = audio_window.latest_audio_frame_count;
          result.latest_audio_channel_count = audio_window.latest_audio_channel_count;
          result.latest_audio_bits_per_sample = audio_window.latest_audio_bits_per_sample;
          result.audio_sample_rate_hz = kYamnetSampleRateHz;
          result.window_ms = options.window_ms;
          result.hop_ms = static_cast<std::uint32_t>((hop_sample_count * 1000) / kYamnetSampleRateHz);
          result.overlap_ratio = static_cast<float>(options.overlap_ratio);
          copy_inference_result(inference_result, result);

          result_publisher.publish(result);
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
    std::cerr << error.what() << '\n';
    return 1;
  }
}
