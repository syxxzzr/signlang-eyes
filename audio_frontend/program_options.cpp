#include "program_options.hpp"

#include "cxxopts.hpp"

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
      if (value > std::numeric_limits<std::uint16_t>::max()) {
        throw std::runtime_error(std::string("--") + option_name + " is outside the supported range");
      }

      return static_cast<std::uint16_t>(value);
    }

    void validate_sample_rate(const std::optional<std::uint32_t>& sample_rate_hz, const char* option_name) {
      if (sample_rate_hz.has_value() && !is_valid_sample_rate(sample_rate_hz.value())) {
        throw std::runtime_error(std::string("--") + option_name + " must be between " +
                                 std::to_string(kMinSampleRateHz) + " and " + std::to_string(kMaxSampleRateHz));
      }
    }

    void validate_channel_count(const std::optional<std::uint16_t>& channel_count, const char* option_name) {
      if (channel_count.has_value() && !is_valid_channel_count(channel_count.value())) {
        throw std::runtime_error(std::string("--") + option_name + " must be between " +
                                 std::to_string(kMinChannelCount) + " and " + std::to_string(kMaxChannelCount));
      }
    }

  } // namespace

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult {
    cxxopts::Options options{
        "signlang_eyes_edgeai_audio_frontend",
        "Capture PCM audio from ALSA and publish it through an iceoryx2 publish-subscribe service."};

    options.add_options()("d,device", "ALSA audio device name", cxxopts::value<std::string>())(
        "s,service", "iceoryx2 publish-subscribe service name", cxxopts::value<std::string>())(
        "p,period-ms", "Audio publish period in milliseconds",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultPublishPeriodMs)))(
        "capture-rate", "Requested ALSA capture sample rate in Hz", cxxopts::value<std::uint32_t>())(
        "capture-channels", "Requested ALSA capture channel count", cxxopts::value<std::uint32_t>())(
        "publish-rate", "Published audio sample rate in Hz", cxxopts::value<std::uint32_t>())(
        "publish-channels", "Published audio channel count", cxxopts::value<std::uint32_t>())(
        "denoise", "Enable lightweight Wiener noise reduction",
        cxxopts::value<bool>()->default_value("false")->implicit_value("true"))("h,help", "Print usage");

    const auto parsed_options = options.parse(argc, argv);
    if (parsed_options.count("help") != 0) {
      return ProgramUsage{.text = options.help()};
    }

    if (parsed_options.count("device") == 0 || parsed_options.count("service") == 0) {
      throw std::runtime_error("Both --device and --service are required.\n\n" + options.help());
    }

    const auto publish_period_ms = parsed_options["period-ms"].as<std::uint32_t>();
    if (publish_period_ms == 0 || publish_period_ms > kMaxPublishPeriodMs) {
      throw std::runtime_error("--period-ms must be between 1 and " + std::to_string(kMaxPublishPeriodMs) + ".\n\n" +
                               options.help());
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

    return ProgramOptionsParseResult{ProgramOptions{
        .audio_device_name = parsed_options["device"].as<std::string>(),
        .service_name = parsed_options["service"].as<std::string>(),
        .publish_period_ms = publish_period_ms,
        .enable_denoise = parsed_options["denoise"].as<bool>(),
        .capture_format = capture_format,
        .publish_format = publish_format,
    }};
  }

} // namespace signlang::audio_frontend
