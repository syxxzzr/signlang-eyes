#include "common/audio_ring_buffer.hpp"
#include "common/fixed_string.hpp"
#include "common/runtime.hpp"
#include "common/time.hpp"
#include "iceoryx_gateway.hpp"
#include "program_options.hpp"
#include "spdlog/spdlog.h"
#include "speech_asr_result.hpp"
#include "whisper_model.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <exception>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace {

  auto ring_capacity_samples(std::uint64_t window_sample_count, std::uint64_t hop_sample_count) -> std::uint64_t {
    const auto minimum_capacity = window_sample_count + std::max(window_sample_count, hop_sample_count);
    constexpr auto one_second = static_cast<std::uint64_t>(signlang::speech_asr::kWhisperSampleRateHz);
    return std::max(minimum_capacity, window_sample_count + one_second);
  }

  void copy_language_code(signlang::speech_asr::AsrLanguage language,
                          std::array<char, signlang::speech_asr::kMaxLanguageCodeLength>& output) {
    signlang::common::copy_fixed_string(signlang::speech_asr::language_code(language), output);
  }

  void copy_inference_result(const signlang::speech_asr::WhisperInferenceResult& inference_result,
                             signlang::speech_asr::SpeechAsrResult& output_result) {
    output_result.model_input_sample_count = inference_result.model_input_sample_count;
    output_result.mel_frame_count = inference_result.mel_frame_count;
    output_result.decoded_token_count = inference_result.decoded_token_count;
    output_result.encoder_time_ms = inference_result.encoder_time_ms;
    output_result.decoder_time_ms = inference_result.decoder_time_ms;
    output_result.inference_time_ms = inference_result.inference_time_ms;
    signlang::common::copy_fixed_string(inference_result.transcript, output_result.transcript);
  }

} // namespace

auto main(int argc, char** argv) -> int {
  using signlang::common::AudioRingBuffer;
  using signlang::common::AudioWindow;
  using signlang::common::hop_samples_for_overlap;
  using signlang::common::samples_for_window_ms;
  using signlang::speech_asr::AsrLanguage;
  using signlang::speech_asr::IpcAudioSubscriber;
  using signlang::speech_asr::IpcResultPublisher;
  using signlang::speech_asr::kWhisperSampleRateHz;
  using signlang::speech_asr::parse_program_options;
  using signlang::speech_asr::SpeechAsrResult;
  using signlang::speech_asr::WhisperModel;

  return signlang::runtime::run_module(argc, argv, parse_program_options, [&](const auto& options) {
    spdlog::info("Starting speech ASR");
    spdlog::info("Language: {}", options.language == AsrLanguage::English ? "English" : "Chinese");
    spdlog::info("Encoder model: {}", options.encoder_model_path);
    spdlog::info("Decoder model: {}", options.decoder_model_path);
    spdlog::info("Window: {}ms, overlap: {:.1f}%", options.window_ms, options.overlap_ratio * 100);

    const auto window_sample_count = samples_for_window_ms(kWhisperSampleRateHz, options.window_ms);
    const auto hop_sample_count = hop_samples_for_overlap(window_sample_count, options.overlap_ratio);
    if (window_sample_count == 0) {
      throw std::runtime_error("ASR window has no samples");
    }

    AudioRingBuffer audio_buffer{ring_capacity_samples(window_sample_count, hop_sample_count), kWhisperSampleRateHz};
    std::atomic_bool should_stop{false};
    std::atomic_bool downstream_active{false};
    std::exception_ptr worker_error = nullptr;
    std::mutex worker_error_mutex;

    auto record_worker_error = [&](std::exception_ptr error) {
      {
        const std::lock_guard<std::mutex> lock{worker_error_mutex};
        if (worker_error == nullptr) {
          worker_error = std::move(error);
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
            if (audio_subscriber.has_value()) {
              spdlog::info("Speech ASR downstream inactive; detaching audio subscriber and clearing buffered audio");
            }
            audio_subscriber.reset();
            audio_buffer.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
          }

          if (!audio_subscriber.has_value()) {
            spdlog::info("Speech ASR attaching audio subscriber to {}", options.audio_service_name);
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
        spdlog::info("Initializing Whisper model");
        WhisperModel model{options};
        spdlog::info("Whisper model loaded successfully");

        IpcResultPublisher result_publisher{options.result_service_name};
        std::optional<std::uint64_t> next_window_start_sample;
        std::uint64_t result_sequence_number = 0;
        auto downstream_was_active = false;

        while (!should_stop.load()) {
          const auto has_downstream = result_publisher.has_subscribers();
          downstream_active.store(has_downstream);
          if (!has_downstream) {
            if (downstream_was_active) {
              spdlog::info("Speech ASR result subscriber disconnected; pausing inference");
              downstream_was_active = false;
            }
            audio_buffer.clear();
            next_window_start_sample = std::nullopt;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
          }
          if (!downstream_was_active) {
            spdlog::info("Speech ASR result subscriber connected; resuming inference");
            downstream_was_active = true;
          }

          AudioWindow audio_window;
          if (!audio_buffer.wait_for_window(next_window_start_sample, window_sample_count, hop_sample_count,
                                            should_stop, audio_window)) {
            break;
          }

          const auto inference_result = model.infer(audio_window, options.language);

          if (!inference_result.transcript.empty()) {
            spdlog::info("Transcript: {}", inference_result.transcript);
          }

          SpeechAsrResult result{};
          result.sequence_number = result_sequence_number++;
          result.timestamp_ns = signlang::common::steady_timestamp_ns();
          result.audio_start_sample_index = audio_window.start_sample_index;
          result.audio_end_sample_index = audio_window.end_sample_index;
          result.latest_audio_sequence_number = audio_window.latest_audio_sequence_number;
          result.latest_audio_timestamp_ns = audio_window.latest_audio_timestamp_ns;
          result.latest_audio_sample_rate_hz = audio_window.latest_audio_sample_rate_hz;
          result.latest_audio_publish_period_ms = audio_window.latest_audio_publish_period_ms;
          result.latest_audio_frame_count = audio_window.latest_audio_frame_count;
          result.latest_audio_channel_count = audio_window.latest_audio_channel_count;
          result.latest_audio_bits_per_sample = audio_window.latest_audio_bits_per_sample;
          result.audio_sample_rate_hz = kWhisperSampleRateHz;
          result.window_ms = options.window_ms;
          result.hop_ms = static_cast<std::uint32_t>((hop_sample_count * 1000) / kWhisperSampleRateHz);
          result.language = options.language;
          result.overlap_ratio = static_cast<float>(options.overlap_ratio);
          copy_language_code(options.language, result.language_code);
          copy_inference_result(inference_result, result);

          result_publisher.publish(result);
          spdlog::info("Published ASR result seq={} transcript_len={} inference_ms={:.2f}", result.sequence_number,
                       inference_result.transcript.size(), inference_result.inference_time_ms);
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

    spdlog::info("Speech ASR stopped");
    return 0;
  });
}
