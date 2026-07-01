#ifndef SIGNLANG_EYES_SPEECH_TTS_ALSA_PLAYBACK_DEVICE_HPP
#define SIGNLANG_EYES_SPEECH_TTS_ALSA_PLAYBACK_DEVICE_HPP

#include <alsa/asoundlib.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace signlang::speech_tts {

  class AlsaPlaybackDevice {
  public:
    AlsaPlaybackDevice(std::string device_name, std::uint32_t sample_rate_hz);
    ~AlsaPlaybackDevice() = default;

    AlsaPlaybackDevice(const AlsaPlaybackDevice&) = delete;
    auto operator=(const AlsaPlaybackDevice&) -> AlsaPlaybackDevice& = delete;
    AlsaPlaybackDevice(AlsaPlaybackDevice&&) = delete;
    auto operator=(AlsaPlaybackDevice&&) -> AlsaPlaybackDevice& = delete;

    [[nodiscard]] auto sample_rate_hz() const -> std::uint32_t;
    [[nodiscard]] auto device_name() const -> const std::string&;

    void play(const float* samples, std::size_t sample_count, const std::function<bool()>& should_cancel);
    void cancel();

  private:
    struct PcmHandleDeleter {
      void operator()(snd_pcm_t* handle) const noexcept;
    };

    using PcmHandle = std::unique_ptr<snd_pcm_t, PcmHandleDeleter>;

    void configure();
    void recover_from_write_error(int error_code);

    PcmHandle pcm_handle_;
    std::string device_name_;
    std::uint32_t sample_rate_hz_;
    std::vector<std::int16_t> pcm_buffer_;
  };

} // namespace signlang::speech_tts

#endif // SIGNLANG_EYES_SPEECH_TTS_ALSA_PLAYBACK_DEVICE_HPP
