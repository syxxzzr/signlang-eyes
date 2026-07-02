#include "alsa_playback_device.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <utility>

namespace signlang::speech_tts {
  namespace {

    constexpr unsigned int kChannelCount = 1;
    constexpr auto kMaxFramesPerWrite = std::size_t{1024};

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
        throw std::runtime_error(alsa_error_message("Failed to allocate ALSA playback hardware parameters",
                                                    allocation_result));
      }
      return HardwareParams{hardware_params};
    }

    auto float_to_s16(float sample) -> std::int16_t {
      const auto clamped = std::clamp(sample, -1.0F, 1.0F);
      return static_cast<std::int16_t>(std::lrint(clamped < 0.0F ? clamped * 32768.0F : clamped * 32767.0F));
    }

  } // namespace

  AlsaPlaybackDevice::AlsaPlaybackDevice(std::string device_name, std::uint32_t sample_rate_hz) :
      device_name_{std::move(device_name)}, sample_rate_hz_{sample_rate_hz} {
    if (sample_rate_hz_ == 0) {
      throw std::runtime_error("ALSA playback sample rate must be greater than 0");
    }

    snd_pcm_t* pcm_handle = nullptr;
    const auto open_result = snd_pcm_open(&pcm_handle, device_name_.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
    if (open_result < 0) {
      throw std::runtime_error(
          alsa_error_message("Failed to open ALSA playback device '" + device_name_ + "'", open_result));
    }

    pcm_handle_.reset(pcm_handle);
    configure();
    pcm_buffer_.resize(kMaxFramesPerWrite);
  }

  void AlsaPlaybackDevice::PcmHandleDeleter::operator()(snd_pcm_t* handle) const noexcept {
    if (handle != nullptr) {
      snd_pcm_close(handle);
    }
  }

  auto AlsaPlaybackDevice::sample_rate_hz() const -> std::uint32_t { return sample_rate_hz_; }

  auto AlsaPlaybackDevice::device_name() const -> const std::string& { return device_name_; }

  void AlsaPlaybackDevice::play(const float* samples, std::size_t sample_count,
                                const std::function<bool()>& should_cancel) {
    auto offset = std::size_t{0};
    while (offset < sample_count) {
      if (should_cancel()) {
        cancel();
        return;
      }

      const auto frame_count = std::min<std::size_t>(kMaxFramesPerWrite, sample_count - offset);
      for (std::size_t index = 0; index < frame_count; ++index) {
        pcm_buffer_[index] = float_to_s16(samples[offset + index]);
      }

      auto frames_written = std::size_t{0};
      while (frames_written < frame_count) {
        if (should_cancel()) {
          cancel();
          return;
        }

        const auto write_result =
            snd_pcm_writei(pcm_handle_.get(), pcm_buffer_.data() + frames_written, frame_count - frames_written);
        if (write_result < 0) {
          recover_from_write_error(static_cast<int>(write_result));
          continue;
        }
        if (write_result == 0) {
          throw std::runtime_error("ALSA playback stream accepted no frames");
        }
        frames_written += static_cast<std::size_t>(write_result);
      }

      offset += frame_count;
    }
  }

  void AlsaPlaybackDevice::cancel() {
    (void)snd_pcm_drop(pcm_handle_.get());
    const auto prepare_result = snd_pcm_prepare(pcm_handle_.get());
    if (prepare_result < 0) {
      throw std::runtime_error(alsa_error_message("Failed to prepare ALSA playback stream after cancel", prepare_result));
    }
  }

  void AlsaPlaybackDevice::configure() {
    auto hardware_params = create_hardware_params();

    auto result = snd_pcm_hw_params_any(pcm_handle_.get(), hardware_params.get());
    if (result < 0) {
      throw std::runtime_error(alsa_error_message("Failed to initialize ALSA playback hardware parameters", result));
    }

    result = snd_pcm_hw_params_set_access(pcm_handle_.get(), hardware_params.get(), SND_PCM_ACCESS_RW_INTERLEAVED);
    if (result < 0) {
      throw std::runtime_error(alsa_error_message("Failed to set ALSA playback access mode", result));
    }

    result = snd_pcm_hw_params_set_format(pcm_handle_.get(), hardware_params.get(), SND_PCM_FORMAT_S16_LE);
    if (result < 0) {
      throw std::runtime_error(alsa_error_message("Failed to set ALSA playback sample format", result));
    }

    auto rate = static_cast<unsigned int>(sample_rate_hz_);
    result = snd_pcm_hw_params_set_rate_near(pcm_handle_.get(), hardware_params.get(), &rate, nullptr);
    if (result < 0) {
      throw std::runtime_error(alsa_error_message("Failed to set ALSA playback sample rate", result));
    }
    if (rate != sample_rate_hz_) {
      throw std::runtime_error("ALSA playback device does not support requested Piper sample rate " +
                               std::to_string(sample_rate_hz_) + " Hz");
    }

    auto channels = kChannelCount;
    result = snd_pcm_hw_params_set_channels_near(pcm_handle_.get(), hardware_params.get(), &channels);
    if (result < 0) {
      throw std::runtime_error(alsa_error_message("Failed to set ALSA playback channel count", result));
    }
    if (channels != kChannelCount) {
      throw std::runtime_error("ALSA playback device does not support mono output");
    }

    result = snd_pcm_hw_params(pcm_handle_.get(), hardware_params.get());
    if (result < 0) {
      throw std::runtime_error(alsa_error_message("Failed to apply ALSA playback hardware parameters", result));
    }

    result = snd_pcm_prepare(pcm_handle_.get());
    if (result < 0) {
      throw std::runtime_error(alsa_error_message("Failed to prepare ALSA playback device", result));
    }
  }

  void AlsaPlaybackDevice::recover_from_write_error(int error_code) {
    const auto recover_result = snd_pcm_recover(pcm_handle_.get(), error_code, 1);
    if (recover_result < 0) {
      throw std::runtime_error(alsa_error_message("Failed to recover ALSA playback stream", recover_result));
    }
  }

} // namespace signlang::speech_tts
