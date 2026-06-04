#include "alsa_capture_device.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace signlang::audio_frontend {
  namespace {

    struct HardwareParamsDeleter {
      void operator()(snd_pcm_hw_params_t* hardware_params) const noexcept { snd_pcm_hw_params_free(hardware_params); }
    };

    using HardwareParams = std::unique_ptr<snd_pcm_hw_params_t, HardwareParamsDeleter>;

    auto alsa_error_message(const std::string& context, int error_code) -> std::string {
      return context + ": " + snd_strerror(error_code);
    }

    auto create_hardware_params() -> HardwareParams {
      snd_pcm_hw_params_t* hardware_params = nullptr;
      const auto allocation_result = snd_pcm_hw_params_malloc(&hardware_params);
      if (allocation_result < 0) {
        throw std::runtime_error(alsa_error_message("Failed to allocate ALSA hardware parameters", allocation_result));
      }

      return HardwareParams{hardware_params};
    }

    auto frames_for_period(std::uint32_t sample_rate_hz, std::uint32_t publish_period_ms) -> snd_pcm_uframes_t {
      if (publish_period_ms == 0 || publish_period_ms > kMaxPublishPeriodMs) {
        throw std::runtime_error("Invalid ALSA capture period");
      }

      return static_cast<snd_pcm_uframes_t>((static_cast<std::uint64_t>(sample_rate_hz) * publish_period_ms) / 1000);
    }

    auto select_auto_sample_rate(snd_pcm_hw_params_t* hardware_params) -> unsigned int {
      unsigned int max_rate = 0;
      const auto result = snd_pcm_hw_params_get_rate_max(hardware_params, &max_rate, nullptr);
      if (result < 0) {
        throw std::runtime_error(alsa_error_message("Failed to query ALSA maximum sample rate", result));
      }

      if (max_rate < kMinSampleRateHz) {
        throw std::runtime_error("ALSA device does not expose a supported capture sample rate");
      }

      return std::min(max_rate, kMaxSampleRateHz);
    }

    auto select_auto_channel_count(snd_pcm_hw_params_t* hardware_params) -> unsigned int {
      unsigned int max_channels = 0;
      const auto result = snd_pcm_hw_params_get_channels_max(hardware_params, &max_channels);
      if (result < 0) {
        throw std::runtime_error(alsa_error_message("Failed to query ALSA maximum channel count", result));
      }

      if (max_channels < kMinChannelCount) {
        throw std::runtime_error("ALSA device does not expose a supported capture channel count");
      }

      return std::min(max_channels, static_cast<unsigned int>(kMaxChannelCount));
    }

  } // namespace

  AlsaCaptureDevice::AlsaCaptureDevice(const std::string& device_name, AudioFormatRequest format_request,
                                       std::uint32_t publish_period_ms) :
      device_name_{device_name}, format_request_{format_request},
      format_{.sample_rate_hz = kDefaultSampleRateHz, .channel_count = kDefaultChannelCount},
      publish_period_ms_{publish_period_ms} {
    snd_pcm_t* pcm_handle = nullptr;
    const auto open_result = snd_pcm_open(&pcm_handle, device_name_.c_str(), SND_PCM_STREAM_CAPTURE, 0);
    if (open_result < 0) {
      throw std::runtime_error(
          alsa_error_message("Failed to open ALSA capture device '" + device_name_ + "'", open_result));
    }

    pcm_handle_.reset(pcm_handle);
    configure();
    sample_buffer_.resize(static_cast<std::size_t>(frames_per_packet_ * format_.channel_count));
  }

  void AlsaCaptureDevice::PcmHandleDeleter::operator()(snd_pcm_t* handle) const noexcept {
    if (handle != nullptr) {
      snd_pcm_close(handle);
    }
  }

  auto AlsaCaptureDevice::format() const -> AudioFormat { return format_; }

  auto AlsaCaptureDevice::capture_samples() -> const std::vector<std::int16_t>& {
    snd_pcm_uframes_t frames_read = 0;
    while (frames_read < frames_per_packet_) {
      auto* write_position = sample_buffer_.data() + static_cast<std::size_t>(frames_read * format_.channel_count);
      const auto frames_remaining = frames_per_packet_ - frames_read;
      const auto read_result = snd_pcm_readi(pcm_handle_.get(), write_position, frames_remaining);

      if (read_result < 0) {
        recover_from_read_error(static_cast<int>(read_result));
        continue;
      }

      if (read_result == 0) {
        throw std::runtime_error("ALSA capture stream returned no frames");
      }

      frames_read += static_cast<snd_pcm_uframes_t>(read_result);
    }

    return sample_buffer_;
  }

  void AlsaCaptureDevice::configure() {
    auto hardware_params = create_hardware_params();

    auto result = snd_pcm_hw_params_any(pcm_handle_.get(), hardware_params.get());
    if (result < 0) {
      throw std::runtime_error(alsa_error_message("Failed to initialize ALSA hardware parameters", result));
    }

    result = snd_pcm_hw_params_set_access(pcm_handle_.get(), hardware_params.get(), SND_PCM_ACCESS_RW_INTERLEAVED);
    if (result < 0) {
      throw std::runtime_error(alsa_error_message("Failed to set ALSA access mode", result));
    }

    result = snd_pcm_hw_params_set_format(pcm_handle_.get(), hardware_params.get(), SND_PCM_FORMAT_S16_LE);
    if (result < 0) {
      throw std::runtime_error(alsa_error_message("Failed to set ALSA sample format", result));
    }

    configure_sample_rate(hardware_params.get());
    configure_channel_count(hardware_params.get());

    frames_per_packet_ = frames_for_period(format_.sample_rate_hz, publish_period_ms_);
    snd_pcm_uframes_t period_size = frames_per_packet_;
    result = snd_pcm_hw_params_set_period_size_near(pcm_handle_.get(), hardware_params.get(), &period_size, nullptr);
    if (result < 0) {
      throw std::runtime_error(alsa_error_message("Failed to set ALSA period size", result));
    }

    result = snd_pcm_hw_params(pcm_handle_.get(), hardware_params.get());
    if (result < 0) {
      throw std::runtime_error(alsa_error_message("Failed to apply ALSA hardware parameters", result));
    }

    result = snd_pcm_prepare(pcm_handle_.get());
    if (result < 0) {
      throw std::runtime_error(alsa_error_message("Failed to prepare ALSA capture device", result));
    }
  }

  void AlsaCaptureDevice::recover_from_read_error(int error_code) {
    const auto recover_result = snd_pcm_recover(pcm_handle_.get(), error_code, 1);
    if (recover_result < 0) {
      throw std::runtime_error(alsa_error_message("Failed to recover ALSA capture stream", recover_result));
    }
  }

  void AlsaCaptureDevice::configure_sample_rate(snd_pcm_hw_params_t* hardware_params) {
    unsigned int sample_rate_hz = format_request_.sample_rate_hz.value_or(select_auto_sample_rate(hardware_params));
    const auto result = snd_pcm_hw_params_set_rate_near(pcm_handle_.get(), hardware_params, &sample_rate_hz, nullptr);
    if (result < 0) {
      throw std::runtime_error(alsa_error_message("Failed to set ALSA sample rate", result));
    }

    if (format_request_.sample_rate_hz.has_value() && sample_rate_hz != format_request_.sample_rate_hz.value()) {
      throw std::runtime_error("ALSA device does not support requested capture sample rate " +
                               std::to_string(format_request_.sample_rate_hz.value()) + " Hz");
    }

    if (!is_valid_sample_rate(sample_rate_hz)) {
      throw std::runtime_error("ALSA selected unsupported capture sample rate " + std::to_string(sample_rate_hz) +
                               " Hz");
    }

    format_.sample_rate_hz = sample_rate_hz;
  }

  void AlsaCaptureDevice::configure_channel_count(snd_pcm_hw_params_t* hardware_params) {
    unsigned int channel_count = format_request_.channel_count.value_or(select_auto_channel_count(hardware_params));
    const auto result = snd_pcm_hw_params_set_channels_near(pcm_handle_.get(), hardware_params, &channel_count);
    if (result < 0) {
      throw std::runtime_error(alsa_error_message("Failed to set ALSA channel count", result));
    }

    if (format_request_.channel_count.has_value() && channel_count != format_request_.channel_count.value()) {
      throw std::runtime_error("ALSA device does not support requested capture channel count " +
                               std::to_string(format_request_.channel_count.value()));
    }

    if (channel_count > std::numeric_limits<std::uint16_t>::max() ||
        !is_valid_channel_count(static_cast<std::uint16_t>(channel_count))) {
      throw std::runtime_error("ALSA selected unsupported capture channel count " + std::to_string(channel_count));
    }

    format_.channel_count = static_cast<std::uint16_t>(channel_count);
  }

} // namespace signlang::audio_frontend
