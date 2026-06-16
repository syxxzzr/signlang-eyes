#include "whisper_model.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace signlang::speech_asr {
  namespace {

    constexpr std::uint32_t kNfft = 400;
    constexpr std::uint32_t kHopLength = 160;
    constexpr std::uint32_t kStftBinCount = (kNfft / 2) + 1;
    constexpr std::uint32_t kExpectedMelBinCount = 80;
    constexpr std::int64_t kEndOfTextToken = 50257;
    constexpr std::int64_t kStartOfTranscriptToken = 50258;
    constexpr std::int64_t kEnglishToken = 50259;
    constexpr std::int64_t kChineseToken = 50260;
    constexpr std::int64_t kTranscribeToken = 50359;
    constexpr std::int64_t kNoTimestampsToken = 50363;
    constexpr std::int64_t kTimestampBeginToken = 50364;

    class RknnOutputReleaseGuard {
    public:
      RknnOutputReleaseGuard(rknn_context context, std::uint32_t output_count, rknn_output* outputs) :
          context_{context}, output_count_{output_count}, outputs_{outputs}, active_{true} {}

      RknnOutputReleaseGuard(const RknnOutputReleaseGuard&) = delete;
      auto operator=(const RknnOutputReleaseGuard&) -> RknnOutputReleaseGuard& = delete;

      RknnOutputReleaseGuard(RknnOutputReleaseGuard&&) = delete;
      auto operator=(RknnOutputReleaseGuard&&) -> RknnOutputReleaseGuard& = delete;

      ~RknnOutputReleaseGuard() {
        if (active_) {
          (void)rknn_outputs_release(context_, output_count_, outputs_);
        }
      }

      auto release() -> int {
        const auto result = rknn_outputs_release(context_, output_count_, outputs_);
        active_ = false;
        return result;
      }

    private:
      rknn_context context_;
      std::uint32_t output_count_;
      rknn_output* outputs_;
      bool active_;
    };

    auto rknn_error(const std::string& context, int error_code) -> std::runtime_error {
      return std::runtime_error(context + ": ret=" + std::to_string(error_code));
    }

    auto steady_elapsed_ms(std::chrono::steady_clock::time_point start_time,
                           std::chrono::steady_clock::time_point end_time) -> float {
      return std::chrono::duration<float, std::milli>(end_time - start_time).count();
    }

    void replace_all(std::string& value, const std::string& from, const std::string& to) {
      if (from.empty()) {
        return;
      }

      std::size_t position = 0;
      while ((position = value.find(from, position)) != std::string::npos) {
        value.replace(position, from.size(), to);
        position += to.size();
      }
    }

    auto base64_value(char value) -> int {
      if (value >= 'A' && value <= 'Z') {
        return value - 'A';
      }
      if (value >= 'a' && value <= 'z') {
        return value - 'a' + 26;
      }
      if (value >= '0' && value <= '9') {
        return value - '0' + 52;
      }
      if (value == '+') {
        return 62;
      }
      if (value == '/') {
        return 63;
      }
      return -1;
    }

    auto decode_base64_token(const std::string& token) -> std::string {
      std::string decoded;
      int accumulator = 0;
      int bits = -8;

      for (char character : token) {
        if (character == '=') {
          break;
        }

        const auto value = base64_value(character);
        if (value < 0) {
          return {};
        }

        accumulator = (accumulator << 6) | value;
        bits += 6;
        if (bits >= 0) {
          decoded.push_back(static_cast<char>((accumulator >> bits) & 0xFF));
          bits -= 8;
        }
      }

      return decoded;
    }

    auto is_special_token(std::int64_t token) -> bool { return token >= kEndOfTextToken; }

  } // namespace

  WhisperModel::WhisperModel(const ProgramOptions& options) :
      encoder_context_{0}, decoder_context_{0}, encoder_io_num_{}, decoder_io_num_{}, mel_bin_count_{0},
      mel_frame_count_{0}, model_input_sample_count_{0}, encoder_output_count_{0}, decoder_token_count_{0},
      vocab_size_{0}, max_decode_steps_{0}, decoder_tokens_input_index_{0}, decoder_audio_input_index_{1},
      fft_input_{nullptr}, fft_output_{nullptr}, fft_plan_{nullptr} {
    load_assets(options);
    initialize_contexts(options);
    validate_model_io();
    allocate_workspaces(options.max_decode_steps);
    build_mel_filter_spans();
    initialize_fft_workspace();
  }

  WhisperModel::~WhisperModel() {
    if (fft_plan_ != nullptr) {
      fftwf_destroy_plan(fft_plan_);
      fft_plan_ = nullptr;
    }
    if (fft_output_ != nullptr) {
      fftwf_free(fft_output_);
      fft_output_ = nullptr;
    }
    if (fft_input_ != nullptr) {
      fftwf_free(fft_input_);
      fft_input_ = nullptr;
    }
    if (decoder_context_ != 0) {
      rknn_destroy(decoder_context_);
      decoder_context_ = 0;
    }
    if (encoder_context_ != 0) {
      rknn_destroy(encoder_context_);
      encoder_context_ = 0;
    }
  }

  auto WhisperModel::infer(const AudioWindow& audio_window, AsrLanguage language) -> WhisperInferenceResult {
    const auto start_time = std::chrono::steady_clock::now();
    prepare_mel_input(audio_window);
    const auto encoder_time_ms = run_encoder();

    std::uint32_t decoded_token_count = 0;
    auto [transcript, decoder_time_ms] = run_decoder(language, decoded_token_count);

    const auto end_time = std::chrono::steady_clock::now();
    return WhisperInferenceResult{
        .transcript = std::move(transcript),
        .model_input_sample_count = model_input_sample_count_,
        .mel_frame_count = mel_frame_count_,
        .decoded_token_count = decoded_token_count,
        .encoder_time_ms = encoder_time_ms,
        .decoder_time_ms = decoder_time_ms,
        .inference_time_ms = steady_elapsed_ms(start_time, end_time),
    };
  }

  void WhisperModel::initialize_contexts(const ProgramOptions& options) {
    initialize_context(options.encoder_model_path, options.encoder_npu_core_mask, options.rknn_priority_flag,
                       encoder_context_, encoder_io_num_, encoder_input_attrs_, encoder_output_attrs_);
    initialize_context(options.decoder_model_path, options.decoder_npu_core_mask, options.rknn_priority_flag,
                       decoder_context_, decoder_io_num_, decoder_input_attrs_, decoder_output_attrs_);
  }

  void WhisperModel::initialize_context(const std::string& model_path, rknn_core_mask npu_core_mask,
                                        std::uint32_t rknn_priority_flag, rknn_context& context,
                                        rknn_input_output_num& io_num,
                                        std::vector<rknn_tensor_attr>& input_attrs,
                                        std::vector<rknn_tensor_attr>& output_attrs) {
    auto* model_path_buffer = const_cast<char*>(model_path.c_str());
    const auto init_result = rknn_init(&context, model_path_buffer, 0, rknn_priority_flag, nullptr);
    if (init_result < 0) {
      throw rknn_error("Failed to initialize RKNN Whisper model " + model_path, init_result);
    }

    const auto core_result = rknn_set_core_mask(context, npu_core_mask);
    if (core_result < 0) {
      throw rknn_error("Failed to set RKNN Whisper NPU core mask", core_result);
    }

    query_model_io(context, io_num, input_attrs, output_attrs);
  }

  void WhisperModel::query_model_io(rknn_context context, rknn_input_output_num& io_num,
                                    std::vector<rknn_tensor_attr>& input_attrs,
                                    std::vector<rknn_tensor_attr>& output_attrs) {
    auto result = rknn_query(context, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (result != RKNN_SUCC) {
      throw rknn_error("Failed to query RKNN Whisper input/output count", result);
    }

    input_attrs.resize(io_num.n_input);
    for (std::uint32_t input_index = 0; input_index < io_num.n_input; ++input_index) {
      input_attrs[input_index] = {};
      input_attrs[input_index].index = input_index;
      result = rknn_query(context, RKNN_QUERY_INPUT_ATTR, &input_attrs[input_index], sizeof(rknn_tensor_attr));
      if (result != RKNN_SUCC) {
        throw rknn_error("Failed to query RKNN Whisper input tensor", result);
      }
    }

    output_attrs.resize(io_num.n_output);
    for (std::uint32_t output_index = 0; output_index < io_num.n_output; ++output_index) {
      output_attrs[output_index] = {};
      output_attrs[output_index].index = output_index;
      result = rknn_query(context, RKNN_QUERY_OUTPUT_ATTR, &output_attrs[output_index], sizeof(rknn_tensor_attr));
      if (result != RKNN_SUCC) {
        throw rknn_error("Failed to query RKNN Whisper output tensor", result);
      }
    }
  }

  void WhisperModel::validate_model_io() {
    if (encoder_io_num_.n_input != 1 || encoder_io_num_.n_output != 1) {
      throw std::runtime_error("Unexpected RKNN Whisper encoder input/output count");
    }
    if (decoder_io_num_.n_input != 2 || decoder_io_num_.n_output != 1) {
      throw std::runtime_error("Unexpected RKNN Whisper decoder input/output count");
    }

    const auto& encoder_input_attr = encoder_input_attrs_[0];
    if (encoder_input_attr.n_elems == 0 || encoder_input_attr.n_elems % kExpectedMelBinCount != 0) {
      throw std::runtime_error("Unexpected RKNN Whisper encoder mel input shape");
    }

    mel_bin_count_ = kExpectedMelBinCount;
    mel_frame_count_ = encoder_input_attr.n_elems / mel_bin_count_;
    model_input_sample_count_ = mel_frame_count_ * kHopLength;
    encoder_output_count_ = encoder_output_attrs_[0].n_elems;
    if (encoder_output_count_ == 0) {
      throw std::runtime_error("RKNN Whisper encoder output tensor has no elements");
    }

    decoder_tokens_input_index_ = decoder_io_num_.n_input;
    decoder_audio_input_index_ = decoder_io_num_.n_input;
    for (std::uint32_t input_index = 0; input_index < decoder_input_attrs_.size(); ++input_index) {
      const auto& attr = decoder_input_attrs_[input_index];
      if (attr.type == RKNN_TENSOR_INT64) {
        decoder_tokens_input_index_ = input_index;
      }
      if (attr.n_elems == encoder_output_count_) {
        decoder_audio_input_index_ = input_index;
      }
    }

    if (decoder_tokens_input_index_ == decoder_io_num_.n_input ||
        decoder_audio_input_index_ == decoder_io_num_.n_input ||
        decoder_tokens_input_index_ == decoder_audio_input_index_) {
      throw std::runtime_error("Failed to identify RKNN Whisper decoder token/audio inputs");
    }

    decoder_token_count_ = decoder_input_attrs_[decoder_tokens_input_index_].n_elems;
    if (decoder_token_count_ < 4 || decoder_output_attrs_[0].n_elems % decoder_token_count_ != 0) {
      throw std::runtime_error("Unexpected RKNN Whisper decoder token or output shape");
    }

    vocab_size_ = decoder_output_attrs_[0].n_elems / decoder_token_count_;
    if (vocab_en_.size() < vocab_size_ || vocab_zh_.size() < vocab_size_) {
      throw std::runtime_error("Whisper vocabulary is smaller than decoder output vocabulary size");
    }
  }

  void WhisperModel::allocate_workspaces(std::uint32_t max_decode_steps) {
    max_decode_steps_ = max_decode_steps;
    mel_input_.resize(encoder_input_attrs_[0].n_elems);
    frame_magnitudes_.resize(kStftBinCount);
    encoder_output_.resize(encoder_output_count_);
    decoder_output_.resize(decoder_output_attrs_[0].n_elems);
    decoder_tokens_.resize(static_cast<std::size_t>(decoder_token_count_) + 1);
  }

  void WhisperModel::load_assets(const ProgramOptions& options) {
    load_mel_filters(options.mel_filters_path);
    vocab_en_ = load_vocab(options.vocab_en_path, false);
    vocab_zh_ = load_vocab(options.vocab_zh_path, true);
  }

  void WhisperModel::load_mel_filters(const std::string& path) {
    std::ifstream input{path};
    if (!input) {
      throw std::runtime_error("Failed to open Whisper mel filters: " + path);
    }

    mel_filters_.clear();
    float value = 0.0F;
    while (input >> value) {
      mel_filters_.push_back(value);
    }

    if (mel_filters_.size() != static_cast<std::size_t>(kExpectedMelBinCount * kStftBinCount)) {
      throw std::runtime_error("Unexpected Whisper mel filter size: " + path);
    }
  }

  auto WhisperModel::load_vocab(const std::string& path, bool base64_decode_tokens) -> std::vector<std::string> {
    std::ifstream input{path};
    if (!input) {
      throw std::runtime_error("Failed to open Whisper vocabulary: " + path);
    }

    std::vector<std::string> vocab;
    std::string line;
    while (std::getline(input, line)) {
      if (line.empty()) {
        continue;
      }

      const auto separator = line.find(' ');
      const auto index_text = separator == std::string::npos ? line : line.substr(0, separator);
      const auto token_text = separator == std::string::npos ? std::string{} : line.substr(separator + 1);

      std::uint32_t token_index = 0;
      try {
        token_index = static_cast<std::uint32_t>(std::stoul(index_text));
      } catch (const std::exception&) {
        throw std::runtime_error("Invalid Whisper vocabulary line: " + line);
      }

      std::string token = token_text;
      if (base64_decode_tokens && !token.empty() && token.front() != '<') {
        token = decode_base64_token(token);
      } else if (!base64_decode_tokens) {
        replace_all(token, std::string{"\xC4\xA0"}, " ");
        replace_all(token, std::string{"\xC4\x8A"}, "\n");
      }

      if (token_index >= vocab.size()) {
        vocab.resize(token_index + 1);
      }
      vocab[token_index] = std::move(token);
    }

    if (vocab.empty()) {
      throw std::runtime_error("Whisper vocabulary is empty: " + path);
    }

    return vocab;
  }

  void WhisperModel::build_mel_filter_spans() {
    mel_filter_spans_.resize(kExpectedMelBinCount);
    for (std::uint32_t mel_index = 0; mel_index < kExpectedMelBinCount; ++mel_index) {
      const auto row_offset = mel_index * kStftBinCount;
      auto begin = kStftBinCount;
      auto end = std::uint32_t{0};
      for (std::uint32_t bin_index = 0; bin_index < kStftBinCount; ++bin_index) {
        if (mel_filters_[row_offset + bin_index] != 0.0F) {
          begin = std::min(begin, bin_index);
          end = std::max(end, bin_index + 1);
        }
      }

      if (begin == kStftBinCount) {
        begin = 0;
        end = 0;
      }
      mel_filter_spans_[mel_index] = MelFilterSpan{.begin = begin, .end = end};
    }
  }

  void WhisperModel::initialize_fft_workspace() {
    hann_window_.resize(kNfft);
    for (std::uint32_t sample_index = 0; sample_index < kNfft; ++sample_index) {
      hann_window_[sample_index] =
          0.5F * (1.0F - std::cos((2.0F * std::numbers::pi_v<float> * static_cast<float>(sample_index)) /
                                  static_cast<float>(kNfft - 1)));
    }

    fft_input_ = static_cast<float*>(fftwf_malloc(sizeof(float) * kNfft));
    fft_output_ = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * kStftBinCount));
    if (fft_input_ == nullptr || fft_output_ == nullptr) {
      if (fft_output_ != nullptr) {
        fftwf_free(fft_output_);
        fft_output_ = nullptr;
      }
      if (fft_input_ != nullptr) {
        fftwf_free(fft_input_);
        fft_input_ = nullptr;
      }
      throw std::runtime_error("Failed to allocate Whisper FFTW workspace");
    }

    fft_plan_ = fftwf_plan_dft_r2c_1d(static_cast<int>(kNfft), fft_input_, fft_output_, FFTW_MEASURE);
    if (fft_plan_ == nullptr) {
      fftwf_free(fft_output_);
      fft_output_ = nullptr;
      fftwf_free(fft_input_);
      fft_input_ = nullptr;
      throw std::runtime_error("Failed to create Whisper FFTW plan");
    }
  }

  void WhisperModel::prepare_mel_input(const AudioWindow& audio_window) {
    if (audio_input_.size() != model_input_sample_count_) {
      audio_input_.resize(model_input_sample_count_);
    }
    std::fill(audio_input_.begin(), audio_input_.end(), 0.0F);
    std::fill(mel_input_.begin(), mel_input_.end(), 0.0F);

    if (audio_window.samples.empty()) {
      return;
    }

    const auto copy_count = std::min(audio_window.samples.size(), audio_input_.size());
    std::copy_n(audio_window.samples.begin(), copy_count, audio_input_.begin());

    compute_log_mel(audio_input_.data(), model_input_sample_count_, mel_frame_count_);
  }

  void WhisperModel::compute_log_mel(const float* audio, std::uint32_t audio_sample_count,
                                     std::uint32_t output_frame_count) {
    auto max_log_value = -std::numeric_limits<float>::infinity();

    for (std::uint32_t frame_index = 0; frame_index < output_frame_count; ++frame_index) {
      const auto frame_start = frame_index * kHopLength;
      for (std::uint32_t sample_index = 0; sample_index < kNfft; ++sample_index) {
        fft_input_[sample_index] =
            sample_with_reflect_pad(audio, audio_sample_count, frame_start + sample_index) * hann_window_[sample_index];
      }

      fftwf_execute(fft_plan_);
      for (std::uint32_t bin_index = 0; bin_index < kStftBinCount; ++bin_index) {
        const auto real = fft_output_[bin_index][0];
        const auto imag = fft_output_[bin_index][1];
        frame_magnitudes_[bin_index] = (real * real) + (imag * imag);
      }

      for (std::uint32_t mel_index = 0; mel_index < mel_bin_count_; ++mel_index) {
        const auto span = mel_filter_spans_[mel_index];
        const auto filter_offset = mel_index * kStftBinCount;
        float mel_value = 0.0F;
        for (std::uint32_t bin_index = span.begin; bin_index < span.end; ++bin_index) {
          mel_value += mel_filters_[filter_offset + bin_index] * frame_magnitudes_[bin_index];
        }

        mel_value = std::max(mel_value, 1.0e-10F);
        const auto log_value = std::log10(mel_value);
        mel_input_[(mel_index * mel_frame_count_) + frame_index] = log_value;
        max_log_value = std::max(max_log_value, log_value);
      }
    }

    const auto threshold = max_log_value - 8.0F;
    for (std::uint32_t mel_index = 0; mel_index < mel_bin_count_; ++mel_index) {
      const auto row_offset = mel_index * mel_frame_count_;
      for (std::uint32_t frame_index = 0; frame_index < output_frame_count; ++frame_index) {
        auto& value = mel_input_[row_offset + frame_index];
        value = (std::max(value, threshold) + 4.0F) * 0.25F;
      }
    }
  }

  auto WhisperModel::sample_with_reflect_pad(const float* audio, std::uint32_t audio_sample_count,
                                             std::uint32_t padded_index) const -> float {
    constexpr auto pad_width = kNfft / 2;
    if (padded_index < pad_width) {
      return audio[std::min<std::uint32_t>(pad_width - 1 - padded_index, audio_sample_count - 1)];
    }

    const auto centered_index = padded_index - pad_width;
    if (centered_index < audio_sample_count) {
      return audio[centered_index];
    }

    const auto tail_index = centered_index - audio_sample_count;
    return audio[audio_sample_count - 1 - std::min(tail_index, audio_sample_count - 1)];
  }

  auto WhisperModel::run_encoder() -> float {
    rknn_input input{};
    input.index = 0;
    input.buf = mel_input_.data();
    input.size = static_cast<std::uint32_t>(mel_input_.size() * sizeof(float));
    input.pass_through = 0;
    input.type = RKNN_TENSOR_FLOAT32;
    input.fmt = encoder_input_attrs_[0].fmt;

    rknn_output output{};
    output.want_float = 1;
    output.is_prealloc = 1;
    output.index = 0;
    output.buf = encoder_output_.data();
    output.size = static_cast<std::uint32_t>(encoder_output_.size() * sizeof(float));

    const auto start_time = std::chrono::steady_clock::now();

    auto result = rknn_inputs_set(encoder_context_, 1, &input);
    if (result < 0) {
      throw rknn_error("Failed to set RKNN Whisper encoder input", result);
    }

    result = rknn_run(encoder_context_, nullptr);
    if (result < 0) {
      throw rknn_error("Failed to run RKNN Whisper encoder", result);
    }

    result = rknn_outputs_get(encoder_context_, 1, &output, nullptr);
    if (result < 0) {
      throw rknn_error("Failed to get RKNN Whisper encoder output", result);
    }
    RknnOutputReleaseGuard output_release_guard{encoder_context_, 1, &output};

    result = output_release_guard.release();
    if (result < 0) {
      throw rknn_error("Failed to release RKNN Whisper encoder output", result);
    }

    const auto end_time = std::chrono::steady_clock::now();
    return steady_elapsed_ms(start_time, end_time);
  }

  auto WhisperModel::run_decoder(AsrLanguage language, std::uint32_t& decoded_token_count)
      -> std::pair<std::string, float> {
    const auto& vocab = vocabulary(language);
    const std::array<std::int64_t, 4> prompt_tokens{
        kStartOfTranscriptToken,
        language_token(language),
        kTranscribeToken,
        kNoTimestampsToken,
    };

    for (std::uint32_t token_index = 0; token_index < decoder_token_count_; ++token_index) {
      decoder_tokens_[token_index] = prompt_tokens[token_index % prompt_tokens.size()];
    }

    auto pop_index = decoder_token_count_;
    std::int64_t next_token = kStartOfTranscriptToken;
    std::string transcript;
    decoded_token_count = 0;

    rknn_input inputs[2]{};
    inputs[decoder_tokens_input_index_].index = decoder_tokens_input_index_;
    inputs[decoder_tokens_input_index_].buf = decoder_tokens_.data();
    inputs[decoder_tokens_input_index_].size =
        static_cast<std::uint32_t>(decoder_token_count_ * sizeof(std::int64_t));
    inputs[decoder_tokens_input_index_].pass_through = 0;
    inputs[decoder_tokens_input_index_].type = RKNN_TENSOR_INT64;
    inputs[decoder_tokens_input_index_].fmt = decoder_input_attrs_[decoder_tokens_input_index_].fmt;

    inputs[decoder_audio_input_index_].index = decoder_audio_input_index_;
    inputs[decoder_audio_input_index_].buf = encoder_output_.data();
    inputs[decoder_audio_input_index_].size = static_cast<std::uint32_t>(encoder_output_.size() * sizeof(float));
    inputs[decoder_audio_input_index_].pass_through = 0;
    inputs[decoder_audio_input_index_].type = RKNN_TENSOR_FLOAT32;
    inputs[decoder_audio_input_index_].fmt = decoder_input_attrs_[decoder_audio_input_index_].fmt;

    rknn_output output{};
    output.want_float = 1;
    output.is_prealloc = 1;
    output.index = 0;
    output.buf = decoder_output_.data();
    output.size = static_cast<std::uint32_t>(decoder_output_.size() * sizeof(float));

    const auto start_time = std::chrono::steady_clock::now();
    for (std::uint32_t step = 0; step < max_decode_steps_ && next_token != kEndOfTextToken; ++step) {
      auto result = rknn_inputs_set(decoder_context_, 2, inputs);
      if (result < 0) {
        throw rknn_error("Failed to set RKNN Whisper decoder inputs", result);
      }

      result = rknn_run(decoder_context_, nullptr);
      if (result < 0) {
        throw rknn_error("Failed to run RKNN Whisper decoder", result);
      }

      result = rknn_outputs_get(decoder_context_, 1, &output, nullptr);
      if (result < 0) {
        throw rknn_error("Failed to get RKNN Whisper decoder output", result);
      }
      RknnOutputReleaseGuard output_release_guard{decoder_context_, 1, &output};

      const auto logits_offset = static_cast<std::size_t>(decoder_token_count_ - 1) * vocab_size_;
      auto best_index = std::uint32_t{0};
      auto best_value = decoder_output_[logits_offset];
      for (std::uint32_t token_index = 1; token_index < vocab_size_; ++token_index) {
        const auto value = decoder_output_[logits_offset + token_index];
        if (value > best_value) {
          best_value = value;
          best_index = token_index;
        }
      }

      result = output_release_guard.release();
      if (result < 0) {
        throw rknn_error("Failed to release RKNN Whisper decoder output", result);
      }

      next_token = static_cast<std::int64_t>(best_index);
      if (next_token == kEndOfTextToken) {
        break;
      }

      if (!is_special_token(next_token) && best_index < vocab.size()) {
        transcript += vocab[best_index];
        ++decoded_token_count;
      }

      if (next_token > kTimestampBeginToken) {
        continue;
      }

      if (pop_index > prompt_tokens.size()) {
        --pop_index;
      }

      decoder_tokens_[decoder_token_count_] = next_token;
      for (std::uint32_t token_index = pop_index; token_index < decoder_token_count_; ++token_index) {
        decoder_tokens_[token_index] = decoder_tokens_[token_index + 1];
      }
    }

    replace_all(transcript, "<|endoftext|>", "");
    replace_all(transcript, "\n", "");

    const auto end_time = std::chrono::steady_clock::now();
    return {std::move(transcript), steady_elapsed_ms(start_time, end_time)};
  }

  auto WhisperModel::vocabulary(AsrLanguage language) const -> const std::vector<std::string>& {
    switch (language) {
    case AsrLanguage::English:
      return vocab_en_;
    case AsrLanguage::Chinese:
      return vocab_zh_;
    }

    return vocab_en_;
  }

  auto WhisperModel::language_token(AsrLanguage language) const -> std::int64_t {
    switch (language) {
    case AsrLanguage::English:
      return kEnglishToken;
    case AsrLanguage::Chinese:
      return kChineseToken;
    }

    return kEnglishToken;
  }

} // namespace signlang::speech_asr
