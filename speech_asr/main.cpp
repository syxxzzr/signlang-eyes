#include "audio_ring_buffer.hpp"
#include "iceoryx_gateway.hpp"
#include "program_options.hpp"
#include "speech_asr_result.hpp"
#include "whisper_model.hpp"

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
#include <string>
#include <thread>
#include <variant>

namespace {

  volatile std::sig_atomic_t g_should_stop = 0;

  void handle_shutdown_signal(int /* signal_number */) { g_should_stop = 1; }

  void install_signal_handlers() {
    std::signal(SIGINT, handle_shutdown_signal);
    std::signal(SIGTERM, handle_shutdown_signal);
  }

  auto ring_capacity_samples(std::uint64_t window_sample_count, std::uint64_t hop_sample_count) -> std::uint64_t {
    const auto minimum_capacity = window_sample_count + std::max(window_sample_count, hop_sample_count);
    const auto one_second = static_cast<std::uint64_t>(signlang::speech_asr::kWhisperSampleRateHz);
    return std::max(minimum_capacity, window_sample_count + one_second);
  }

  auto steady_timestamp_ns() -> std::uint64_t {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
  }

  void copy_string(const std::string& source, std::array<char, signlang::speech_asr::kMaxTranscriptLength>& output) {
    output.fill('\0');
    const auto copy_size = std::min(source.size(), output.size() - 1);
    std::copy_n(source.data(), copy_size, output.data());
  }

  void copy_language_code(signlang::speech_asr::AsrLanguage language,
                          std::array<char, signlang::speech_asr::kMaxLanguageCodeLength>& output) {
    output.fill('\0');
    const auto* code = signlang::speech_asr::language_code(language);
    const auto code_length = std::char_traits<char>::length(code);
    const auto copy_size = std::min<std::size_t>(code_length, output.size() - 1);
    std::copy_n(code, copy_size, output.data());
  }

  void copy_inference_result(const signlang::speech_asr::WhisperInferenceResult& inference_result,
                             signlang::speech_asr::SpeechAsrResult& output_result) {
    output_result.model_input_sample_count = inference_result.model_input_sample_count;
    output_result.mel_frame_count = inference_result.mel_frame_count;
    output_result.decoded_token_count = inference_result.decoded_token_count;
    output_result.encoder_time_ms = inference_result.encoder_time_ms;
    output_result.decoder_time_ms = inference_result.decoder_time_ms;
    output_result.inference_time_ms = inference_result.inference_time_ms;
    copy_string(inference_result.transcript, output_result.transcript);
  }

} // namespace

auto main(int argc, char** argv) -> int {
  using signlang::speech_asr::AudioRingBuffer;
  using signlang::speech_asr::AudioWindow;
  using signlang::speech_asr::hop_samples_for_overlap;
  using signlang::speech_asr::IpcAudioSubscriber;
  using signlang::speech_asr::IpcEnableClient;
  using signlang::speech_asr::IpcResultPublisher;
  using signlang::speech_asr::kWhisperSampleRateHz;
  using signlang::speech_asr::parse_program_options;
  using signlang::speech_asr::ProgramOptions;
  using signlang::speech_asr::ProgramUsage;
  using signlang::speech_asr::samples_for_window_ms;
  using signlang::speech_asr::SpeechAsrResult;
  using signlang::speech_asr::WhisperModel;

  try {
    const auto parse_result = parse_program_options(argc, argv);
    if (const auto* usage = std::get_if<ProgramUsage>(&parse_result); usage != nullptr) {
      std::cout << usage->text << '\n';
      return 0;
    }

    const auto options = std::get<ProgramOptions>(parse_result);
    install_signal_handlers();

    const auto window_sample_count = samples_for_window_ms(kWhisperSampleRateHz, options.window_ms);
    const auto hop_sample_count = hop_samples_for_overlap(window_sample_count, options.overlap_ratio);
    if (window_sample_count == 0) {
      throw std::runtime_error("ASR window has no samples");
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
        WhisperModel model{options};
        IpcResultPublisher result_publisher{options.result_service_name};
        IpcEnableClient enable_client{options.enable_service_name,
                                      std::chrono::milliseconds(options.enable_request_timeout_ms)};
        AudioWindow audio_window;
        std::optional<std::uint64_t> next_window_start_sample;
        std::uint64_t result_sequence_number = 0;
        std::uint64_t enable_request_sequence_number = 0;

        while (audio_buffer.wait_for_window(next_window_start_sample, window_sample_count, hop_sample_count,
                                            should_stop, audio_window)) {
          const auto enable_response = enable_client.query_enabled(enable_request_sequence_number++);
          if (enable_response.enabled) {
            const auto inference_result = model.infer(audio_window, enable_response.language);

            SpeechAsrResult result{};
            result.sequence_number = result_sequence_number++;
            result.timestamp_ns = steady_timestamp_ns();
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
            result.language = enable_response.language;
            result.overlap_ratio = static_cast<float>(options.overlap_ratio);
            copy_language_code(enable_response.language, result.language_code);
            copy_inference_result(inference_result, result);

            result_publisher.publish(result);
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
    std::cerr << error.what() << '\n';
    return 1;
  }
}
