#ifndef SIGNLANG_EYES_SPEECH_TTS_PIPER_PHONEMIZER_HPP
#define SIGNLANG_EYES_SPEECH_TTS_PIPER_PHONEMIZER_HPP

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace signlang::speech_tts {

  struct PiperVoiceConfig {
    std::uint32_t sample_rate_hz;
    float noise_scale;
    float length_scale;
    float noise_w;
    std::uint32_t num_speakers;
    std::unordered_map<std::string, std::vector<std::int64_t>> phoneme_id_map;
  };

  [[nodiscard]] auto load_piper_voice_config(const std::string& config_path) -> PiperVoiceConfig;

  class PiperPhonemizer {
  public:
    PiperPhonemizer(PiperVoiceConfig config, const std::string& dictionary_path);

    [[nodiscard]] auto phonemize_to_ids(const std::string& text) const -> std::vector<std::int64_t>;
    [[nodiscard]] auto config() const -> const PiperVoiceConfig&;

  private:
    [[nodiscard]] auto has_token(const std::string& token) const -> bool;
    void append_token_ids(const std::string& token, std::vector<std::int64_t>& ids) const;
    void append_pinyin_syllable(const std::string& syllable, std::vector<std::int64_t>& ids,
                                std::vector<std::string>& debug_tokens) const;

    PiperVoiceConfig config_;
  };

} // namespace signlang::speech_tts

#endif // SIGNLANG_EYES_SPEECH_TTS_PIPER_PHONEMIZER_HPP
