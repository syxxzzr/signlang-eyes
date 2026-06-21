#include "alsa_capture_device.hpp"
#include "audio_processor.hpp"
#include "audio_publisher.hpp"
#include "common/logging.hpp"
#include "program_options.hpp"
#include "sound_source_blackboard.hpp"
#include "sound_source_localization.hpp"
#include "spdlog/spdlog.h"

#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <variant>

namespace {

  volatile std::sig_atomic_t g_should_stop = 0;

  void handle_shutdown_signal(int /* signal_number */) { g_should_stop = 1; }

  void install_signal_handlers() {
    std::signal(SIGINT, handle_shutdown_signal);
    std::signal(SIGTERM, handle_shutdown_signal);
  }

  auto steady_timestamp_ns() -> std::uint64_t {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
  }

  void validate_publish_format(const signlang::audio_frontend::AudioFormat& capture_format,
                               const signlang::audio_frontend::AudioFormat& publish_format) {
    if (publish_format.channel_count > capture_format.channel_count) {
      throw std::runtime_error("Published channel count must be less than or equal to captured channel count");
    }

    if (publish_format.sample_rate_hz > capture_format.sample_rate_hz) {
      throw std::runtime_error("Published sample rate must be less than or equal to captured sample rate");
    }
  }

} // namespace

auto main(int argc, char** argv) -> int {
  using signlang::audio_frontend::AlsaCaptureDevice;
  using signlang::audio_frontend::AudioFormat;
  using signlang::audio_frontend::AudioProcessor;
  using signlang::audio_frontend::AudioPublisher;
  using signlang::audio_frontend::parse_program_options;
  using signlang::audio_frontend::ProgramOptions;
  using signlang::audio_frontend::ProgramUsage;
  using signlang::audio_frontend::SoundSourceBlackboardPublisher;
  using signlang::audio_frontend::SoundSourceLocalizer;

  signlang::logging::initialize();

  try {
    const auto parse_result = parse_program_options(argc, argv);
    if (const auto* usage = std::get_if<ProgramUsage>(&parse_result); usage != nullptr) {
      std::cout << usage->text << '\n';
      return 0;
    }

    const auto& options = std::get<ProgramOptions>(parse_result);
    signlang::logging::initialize(options.logging);
    install_signal_handlers();

    spdlog::info("Starting audio frontend");
    spdlog::info("Device: {}", options.audio_device_name);
    if (options.capture_format.sample_rate_hz.has_value() && options.capture_format.channel_count.has_value()) {
      spdlog::info("Requested capture: {}Hz, {} channels",
                   options.capture_format.sample_rate_hz.value(), options.capture_format.channel_count.value());
    }

    AlsaCaptureDevice capture_device{options.audio_device_name, options.capture_format, options.publish_period_ms};
    const auto capture_format = capture_device.format();
    spdlog::info("Actual capture: {}Hz, {} channels, period: {}ms",
                 capture_format.sample_rate_hz, capture_format.channel_count, options.publish_period_ms);

    const AudioFormat publish_format{
        .sample_rate_hz =
            options.publish_format.sample_rate_hz.value_or(signlang::audio_frontend::kDefaultSampleRateHz),
        .channel_count = options.publish_format.channel_count.value_or(signlang::audio_frontend::kDefaultChannelCount),
    };
    validate_publish_format(capture_format, publish_format);
    spdlog::info("Output format: {}Hz, {} channels", publish_format.sample_rate_hz, publish_format.channel_count);

    AudioProcessor audio_processor{capture_format, publish_format, options.publish_period_ms};
    AudioPublisher publisher{options.service_name};
    SoundSourceLocalizer sound_source_localizer{options.localization_tdoa_weight, options.localization_rms_weight};
    std::unique_ptr<SoundSourceBlackboardPublisher> sound_source_blackboard;
    if (options.localization_blackboard_service_name.has_value()) {
      sound_source_blackboard =
          std::make_unique<SoundSourceBlackboardPublisher>(options.localization_blackboard_service_name.value());
    }

    std::uint64_t sequence_number = 0;
    while (g_should_stop == 0) {
      const auto& input_samples = capture_device.capture_samples();
      const auto current_sequence_number = sequence_number++;
      if (sound_source_blackboard != nullptr) {
        const auto localization = sound_source_localizer.estimate(input_samples, capture_format, current_sequence_number,
                                                                  steady_timestamp_ns());
        sound_source_blackboard->publish(localization);
      }
      publisher.publish(input_samples, audio_processor, current_sequence_number);
    }

    return 0;
  } catch (const std::exception& error) {
    spdlog::error("{}", error.what());
    return 1;
  }
}
