#ifndef SIGNLANG_EYES_SPEECH_ASR_WHISPER_MODEL_HPP
#define SIGNLANG_EYES_SPEECH_ASR_WHISPER_MODEL_HPP

#include "common/audio_ring_buffer.hpp"
#include "program_options.hpp"
#include "speech_asr_result.hpp"

#include "fftw3.h"
#include "rknn_api.h"

#include <cstdint>
#include <string>
#include <vector>

namespace signlang::speech_asr {

  using signlang::common::AudioWindow;

  struct WhisperInferenceResult {
    std::string transcript;
    std::uint32_t model_input_sample_count;
    std::uint32_t mel_frame_count;
    std::uint32_t decoded_token_count;
    float encoder_time_ms;
    float decoder_time_ms;
    float inference_time_ms;
  };

  class WhisperModel {
  public:
    WhisperModel(const ProgramOptions& options);
    ~WhisperModel();

    WhisperModel(const WhisperModel&) = delete;
    auto operator=(const WhisperModel&) -> WhisperModel& = delete;
    WhisperModel(WhisperModel&&) = delete;
    auto operator=(WhisperModel&&) -> WhisperModel& = delete;

    auto infer(const AudioWindow& audio_window, AsrLanguage language) -> WhisperInferenceResult;

  private:
    struct MelFilterSpan {
      std::uint32_t begin;
      std::uint32_t end;
    };

    void initialize_contexts(const ProgramOptions& options);
    void initialize_context(const std::string& model_path, rknn_core_mask npu_core_mask,
                            std::uint32_t rknn_priority_flag, rknn_context& context,
                            rknn_input_output_num& io_num, std::vector<rknn_tensor_attr>& input_attrs,
                            std::vector<rknn_tensor_attr>& output_attrs);
    void query_model_io(rknn_context context, rknn_input_output_num& io_num,
                        std::vector<rknn_tensor_attr>& input_attrs, std::vector<rknn_tensor_attr>& output_attrs);
    void validate_model_io();
    void allocate_workspaces(std::uint32_t max_decode_steps);
    void load_assets(const ProgramOptions& options);
    void load_mel_filters(const std::string& path);
    auto load_vocab(const std::string& path, bool base64_decode_tokens) -> std::vector<std::string>;
    void build_mel_filter_spans();
    void initialize_fft_workspace();
    void prepare_mel_input(const AudioWindow& audio_window);
    void compute_log_mel(const float* audio, std::uint32_t audio_sample_count, std::uint32_t output_frame_count);
    auto sample_with_reflect_pad(const float* audio, std::uint32_t audio_sample_count, std::uint32_t padded_index) const
        -> float;
    auto run_encoder() -> float;
    auto run_decoder(AsrLanguage language, std::uint32_t& decoded_token_count) -> std::pair<std::string, float>;
    auto vocabulary(AsrLanguage language) const -> const std::vector<std::string>&;
    auto language_token(AsrLanguage language) const -> std::int64_t;

    rknn_context encoder_context_;
    rknn_context decoder_context_;
    rknn_input_output_num encoder_io_num_;
    rknn_input_output_num decoder_io_num_;
    std::vector<rknn_tensor_attr> encoder_input_attrs_;
    std::vector<rknn_tensor_attr> encoder_output_attrs_;
    std::vector<rknn_tensor_attr> decoder_input_attrs_;
    std::vector<rknn_tensor_attr> decoder_output_attrs_;
    std::uint32_t mel_bin_count_;
    std::uint32_t mel_frame_count_;
    std::uint32_t model_input_sample_count_;
    std::uint32_t encoder_output_count_;
    std::uint32_t decoder_token_count_;
    std::uint32_t vocab_size_;
    std::uint32_t max_decode_steps_;
    std::uint32_t decoder_tokens_input_index_;
    std::uint32_t decoder_audio_input_index_;
    std::vector<float> mel_filters_;
    std::vector<MelFilterSpan> mel_filter_spans_;
    std::vector<std::string> vocab_en_;
    std::vector<std::string> vocab_zh_;
    std::vector<float> hann_window_;
    std::vector<float> audio_input_;
    std::vector<float> mel_input_;
    std::vector<float> frame_magnitudes_;
    std::vector<float> encoder_output_;
    std::vector<float> decoder_output_;
    std::vector<std::int64_t> decoder_tokens_;
    float* fft_input_;
    fftwf_complex* fft_output_;
    fftwf_plan fft_plan_;
  };

} // namespace signlang::speech_asr

#endif // SIGNLANG_EYES_SPEECH_ASR_WHISPER_MODEL_HPP
