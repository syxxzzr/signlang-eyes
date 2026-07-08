#ifndef SIGNLANG_EYES_SPEECH_TTS_PIPER_SYNTHESIZER_HPP
#define SIGNLANG_EYES_SPEECH_TTS_PIPER_SYNTHESIZER_HPP

#include "piper_phonemizer.hpp"
#include "program_options.hpp"

#include "rknn_api.h"

#include <onnxruntime_cxx_api.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace signlang::speech_tts {

  struct PiperAudioChunkView {
    const float* samples;
    std::size_t sample_count;
    std::uint32_t sample_rate_hz;
    bool is_last;
  };

  class PiperSynthesizer {
  public:
    explicit PiperSynthesizer(const ProgramOptions& options);
    ~PiperSynthesizer();

    PiperSynthesizer(const PiperSynthesizer&) = delete;
    auto operator=(const PiperSynthesizer&) -> PiperSynthesizer& = delete;
    PiperSynthesizer(PiperSynthesizer&&) = delete;
    auto operator=(PiperSynthesizer&&) -> PiperSynthesizer& = delete;

    void synthesize(const std::string& text, const std::function<bool()>& should_cancel,
                    const std::function<bool(const PiperAudioChunkView&)>& on_chunk);

  private:
    void initialize_decoder(const ProgramOptions& options);
    void query_decoder_io();
    void validate_decoder_io();
    void allocate_decoder_workspaces();
    [[nodiscard]] auto run_encoder(const std::vector<std::int64_t>& phoneme_ids)
        -> std::pair<std::vector<float>, std::vector<float>>;
    [[nodiscard]] auto run_decoder(const std::vector<float>& z, const std::vector<float>& y_mask) -> std::vector<float>;

    PiperPhonemizer phonemizer_;
    Ort::Env onnx_env_;
    Ort::SessionOptions onnx_session_options_;
    Ort::Session encoder_session_;
    Ort::MemoryInfo onnx_memory_info_;
    rknn_context decoder_context_;
    rknn_input_output_num decoder_io_num_;
    std::vector<rknn_tensor_attr> decoder_input_attrs_;
    std::vector<rknn_tensor_attr> decoder_output_attrs_;
    std::uint32_t decoder_z_input_index_;
    std::uint32_t decoder_mask_input_index_;
    std::uint32_t decoder_audio_output_index_;
    std::size_t decoder_z_channel_count_;
    std::size_t decoder_z_element_count_;
    std::size_t decoder_mask_element_count_;
    std::vector<float> decoder_z_input_;
    std::vector<float> decoder_mask_input_;
    std::vector<float> decoder_audio_output_;
    std::vector<rknn_output> decoder_outputs_;
  };

} // namespace signlang::speech_tts

#endif // SIGNLANG_EYES_SPEECH_TTS_PIPER_SYNTHESIZER_HPP
