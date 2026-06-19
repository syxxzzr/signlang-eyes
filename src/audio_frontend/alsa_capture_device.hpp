#ifndef SIGNLANG_EYES_AUDIO_FRONTEND_ALSA_CAPTURE_DEVICE_HPP
#define SIGNLANG_EYES_AUDIO_FRONTEND_ALSA_CAPTURE_DEVICE_HPP

#include "audio_frame.hpp"

#include <alsa/asoundlib.h>

#include <memory>
#include <string>
#include <vector>

namespace signlang::audio_frontend {

  class AlsaCaptureDevice {
  public:
    AlsaCaptureDevice(const std::string& device_name, AudioFormatRequest format_request,
                      std::uint32_t publish_period_ms);
    ~AlsaCaptureDevice() = default;

    AlsaCaptureDevice(const AlsaCaptureDevice&) = delete;
    auto operator=(const AlsaCaptureDevice&) -> AlsaCaptureDevice& = delete;
    AlsaCaptureDevice(AlsaCaptureDevice&&) = delete;
    auto operator=(AlsaCaptureDevice&&) -> AlsaCaptureDevice& = delete;

    auto format() const -> AudioFormat;
    auto capture_samples() -> const std::vector<std::int16_t>&;

  private:
    struct PcmHandleDeleter {
      void operator()(snd_pcm_t* handle) const noexcept;
    };

    using PcmHandle = std::unique_ptr<snd_pcm_t, PcmHandleDeleter>;

    void configure();
    void configure_sample_rate(snd_pcm_hw_params_t* hardware_params);
    void configure_channel_count(snd_pcm_hw_params_t* hardware_params);
    void recover_from_read_error(int error_code);

    PcmHandle pcm_handle_;
    std::string device_name_;
    AudioFormatRequest format_request_;
    AudioFormat format_;
    std::uint32_t publish_period_ms_;
    snd_pcm_uframes_t frames_per_packet_;
    std::vector<std::int16_t> sample_buffer_;
  };

} // namespace signlang::audio_frontend

#endif // SIGNLANG_EYES_AUDIO_FRONTEND_ALSA_CAPTURE_DEVICE_HPP
