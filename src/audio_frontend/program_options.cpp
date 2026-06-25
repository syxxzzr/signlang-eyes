#include "program_options.hpp"

#include "sound_source_localization.hpp"

#include "common/logging_cli.hpp"
#include "cxxopts.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace signlang::audio_frontend {
  namespace {

    auto optional_u32(const cxxopts::ParseResult& parsed_options, const char* option_name)
        -> std::optional<std::uint32_t> {
      if (parsed_options.count(option_name) == 0) {
        return std::nullopt;
      }

      return parsed_options[option_name].as<std::uint32_t>();
    }

    auto optional_u16(const cxxopts::ParseResult& parsed_options, const char* option_name)
        -> std::optional<std::uint16_t> {
      if (parsed_options.count(option_name) == 0) {
        return std::nullopt;
      }

      const auto value = parsed_options[option_name].as<std::uint32_t>();
      if (value == 0) {
        throw std::runtime_error(std::string("--") + option_name + " must be greater than 0");
      }
      if (value > std::numeric_limits<std::uint16_t>::max()) {
        throw std::runtime_error(std::string("--") + option_name + " is outside the supported range");
      }

      return static_cast<std::uint16_t>(value);
    }

    void validate_sample_rate(const std::optional<std::uint32_t>& sample_rate_hz, const char* option_name) {
      if (sample_rate_hz.has_value() && sample_rate_hz.value() == 0) {
        throw std::runtime_error(std::string("--") + option_name + " must be greater than 0");
      }
    }

    void validate_channel_count(const std::optional<std::uint16_t>& channel_count, const char* option_name) {
      if (channel_count.has_value() && channel_count.value() == 0) {
        throw std::runtime_error(std::string("--") + option_name + " must be greater than 0");
      }
    }

    void validate_localization_weight(float weight, const char* option_name) {
      if (!std::isfinite(weight) || weight < 0.0F || weight > 1.0F) {
        throw std::runtime_error(std::string("--") + option_name + " must be between 0.0 and 1.0");
      }
    }

    void validate_localization_weight_sum(float tdoa_weight, float rms_weight) {
      constexpr float kWeightSumTolerance = 0.0001F;
      if (std::fabs((tdoa_weight + rms_weight) - 1.0F) > kWeightSumTolerance) {
        throw std::runtime_error("--localization-tdoa-weight and --localization-rms-weight must sum to 1.0");
      }
    }

  } // namespace

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult {
    cxxopts::Options options{
        "signlang_eyes_audio_frontend",
        "Capture PCM audio from ALSA and publish it through an iceoryx2 publish-subscribe service."};

    options.add_options()("d,device", "ALSA audio device name", cxxopts::value<std::string>())(
        "s,service", "iceoryx2 publish-subscribe service name", cxxopts::value<std::string>())(
        "localization-blackboard", "iceoryx2 blackboard service name for sound source channel proximity",
        cxxopts::value<std::string>())(
        "localization-tdoa-weight", "TDOA weight for sound source proximity fusion",
        cxxopts::value<float>()->default_value(std::to_string(kDefaultLocalizationTdoaWeight)))(
        "localization-rms-weight", "RMS energy weight for sound source proximity fusion",
        cxxopts::value<float>()->default_value(std::to_string(kDefaultLocalizationRmsWeight)))(
        "p,period-ms", "Audio publish period in milliseconds",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultPublishPeriodMs)))(
        "capture-rate", "Requested ALSA capture sample rate in Hz", cxxopts::value<std::uint32_t>())(
        "capture-channels", "Requested ALSA capture channel count", cxxopts::value<std::uint32_t>())(
        "publish-rate", "Published audio sample rate in Hz", cxxopts::value<std::uint32_t>())(
        "publish-channels", "Published audio channel count", cxxopts::value<std::uint32_t>())("h,help", "Print usage");
    signlang::logging::add_cli_options(options);

    const auto parsed_options = options.parse(argc, argv);
    if (parsed_options.count("help") != 0) {
      return ProgramUsage{.text = options.help()};
    }

    if (parsed_options.count("device") == 0 || parsed_options.count("service") == 0) {
      throw std::runtime_error("Both --device and --service are required.\n\n" + options.help());
    }

    const auto publish_period_ms = parsed_options["period-ms"].as<std::uint32_t>();
    if (publish_period_ms == 0) {
      throw std::runtime_error("--period-ms must be greater than 0");
    }

    const AudioFormatRequest capture_format{
        .sample_rate_hz = optional_u32(parsed_options, "capture-rate"),
        .channel_count = optional_u16(parsed_options, "capture-channels"),
    };
    const AudioFormatRequest publish_format{
        .sample_rate_hz = optional_u32(parsed_options, "publish-rate"),
        .channel_count = optional_u16(parsed_options, "publish-channels"),
    };

    validate_sample_rate(capture_format.sample_rate_hz, "capture-rate");
    validate_channel_count(capture_format.channel_count, "capture-channels");
    validate_sample_rate(publish_format.sample_rate_hz, "publish-rate");
    validate_channel_count(publish_format.channel_count, "publish-channels");

    const auto localization_tdoa_weight = parsed_options["localization-tdoa-weight"].as<float>();
    const auto localization_rms_weight = parsed_options["localization-rms-weight"].as<float>();
    validate_localization_weight(localization_tdoa_weight, "localization-tdoa-weight");
    validate_localization_weight(localization_rms_weight, "localization-rms-weight");
    validate_localization_weight_sum(localization_tdoa_weight, localization_rms_weight);

    return ProgramOptionsParseResult{ProgramOptions{
        .audio_device_name = parsed_options["device"].as<std::string>(),
        .service_name = parsed_options["service"].as<std::string>(),
        .localization_blackboard_service_name = parsed_options.count("localization-blackboard") == 0
            ? std::nullopt
            : std::optional<std::string>{parsed_options["localization-blackboard"].as<std::string>()},
        .localization_tdoa_weight = localization_tdoa_weight,
        .localization_rms_weight = localization_rms_weight,
        .publish_period_ms = publish_period_ms,
        .capture_format = capture_format,
        .publish_format = publish_format,
        .logging = signlang::logging::parse_cli_options(parsed_options),
    }};
  }

} // namespace signlang::audio_frontend
