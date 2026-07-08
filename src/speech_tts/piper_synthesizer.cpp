#include "piper_synthesizer.hpp"

#include "spdlog/spdlog.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace signlang::speech_tts {
  namespace {

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

    [[nodiscard]] auto rknn_error(const std::string& context, int error_code) -> std::runtime_error {
      return std::runtime_error(context + ": ret=" + std::to_string(error_code));
    }

    [[nodiscard]] auto tensor_last_dim(const std::vector<int64_t>& shape) -> std::size_t {
      if (shape.empty() || shape.back() <= 0) {
        throw std::runtime_error("Unexpected Piper encoder output shape");
      }
      return static_cast<std::size_t>(shape.back());
    }

  } // namespace

  PiperSynthesizer::PiperSynthesizer(const ProgramOptions& options) :
      phonemizer_{load_piper_voice_config(options.config_path), options.pinyin_dictionary_path},
      onnx_env_{ORT_LOGGING_LEVEL_WARNING, "signlang_eyes_speech_tts"},
      onnx_session_options_{},
      encoder_session_{nullptr},
      onnx_memory_info_{Ort::MemoryInfo::CreateCpu(OrtAllocatorType::OrtArenaAllocator, OrtMemTypeDefault)},
      decoder_context_{0},
      decoder_io_num_{},
      decoder_z_input_index_{0},
      decoder_mask_input_index_{1},
      decoder_audio_output_index_{0},
      decoder_z_channel_count_{0},
      decoder_z_element_count_{0},
      decoder_mask_element_count_{0} {
    onnx_session_options_.SetIntraOpNumThreads(1);
    onnx_session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
    encoder_session_ = Ort::Session{onnx_env_, options.encoder_model_path.c_str(), onnx_session_options_};
    initialize_decoder(options);
  }

  PiperSynthesizer::~PiperSynthesizer() {
    if (decoder_context_ != 0) {
      rknn_destroy(decoder_context_);
      decoder_context_ = 0;
    }
  }

  void PiperSynthesizer::synthesize(const std::string& text, const std::function<bool()>& should_cancel,
                                    const std::function<bool(const PiperAudioChunkView&)>& on_chunk) {
    if (should_cancel()) {
      return;
    }

    const auto phoneme_ids = phonemizer_.phonemize_to_ids(text);
    spdlog::debug("Piper phonemized text bytes={} phoneme_ids={}", text.size(), phoneme_ids.size());
    if (should_cancel()) {
      return;
    }

    auto [z, y_mask] = run_encoder(phoneme_ids);
    spdlog::debug("Piper encoder output z={} y_mask={}", z.size(), y_mask.size());
    if (should_cancel()) {
      return;
    }

    auto audio = run_decoder(z, y_mask);
    if (should_cancel()) {
      return;
    }

    const auto chunk = PiperAudioChunkView{audio.data(), audio.size(), phonemizer_.config().sample_rate_hz, true};
    (void)on_chunk(chunk);
  }

  void PiperSynthesizer::initialize_decoder(const ProgramOptions& options) {
    auto* model_path_buffer = const_cast<char*>(options.decoder_model_path.c_str());
    const auto init_result =
        rknn_init(&decoder_context_, model_path_buffer, 0, options.rknn_priority_flag, nullptr);
    if (init_result < 0) {
      throw rknn_error("Failed to initialize RKNN Piper decoder model " + options.decoder_model_path, init_result);
    }

    const auto core_result = rknn_set_core_mask(decoder_context_, options.decoder_npu_core_mask);
    if (core_result < 0) {
      throw rknn_error("Failed to set RKNN Piper decoder NPU core mask", core_result);
    }

    query_decoder_io();
    validate_decoder_io();
    allocate_decoder_workspaces();
  }

  void PiperSynthesizer::query_decoder_io() {
    auto result = rknn_query(decoder_context_, RKNN_QUERY_IN_OUT_NUM, &decoder_io_num_, sizeof(decoder_io_num_));
    if (result != RKNN_SUCC) {
      throw rknn_error("Failed to query RKNN Piper decoder input/output count", result);
    }

    decoder_input_attrs_.resize(decoder_io_num_.n_input);
    for (std::uint32_t input_index = 0; input_index < decoder_input_attrs_.size(); ++input_index) {
      decoder_input_attrs_[input_index] = {};
      decoder_input_attrs_[input_index].index = input_index;
      result = rknn_query(decoder_context_, RKNN_QUERY_INPUT_ATTR, &decoder_input_attrs_[input_index],
                          sizeof(rknn_tensor_attr));
      if (result != RKNN_SUCC) {
        throw rknn_error("Failed to query RKNN Piper decoder input tensor", result);
      }
    }

    decoder_output_attrs_.resize(decoder_io_num_.n_output);
    for (std::uint32_t output_index = 0; output_index < decoder_output_attrs_.size(); ++output_index) {
      decoder_output_attrs_[output_index] = {};
      decoder_output_attrs_[output_index].index = output_index;
      result = rknn_query(decoder_context_, RKNN_QUERY_OUTPUT_ATTR, &decoder_output_attrs_[output_index],
                          sizeof(rknn_tensor_attr));
      if (result != RKNN_SUCC) {
        throw rknn_error("Failed to query RKNN Piper decoder output tensor", result);
      }
    }
  }

  void PiperSynthesizer::validate_decoder_io() {
    if (decoder_io_num_.n_input != 2 || decoder_io_num_.n_output != 1) {
      throw std::runtime_error("Unexpected RKNN Piper decoder input/output count");
    }

    decoder_z_input_index_ = decoder_io_num_.n_input;
    decoder_mask_input_index_ = decoder_io_num_.n_input;
    for (std::uint32_t input_index = 0; input_index < decoder_input_attrs_.size(); ++input_index) {
      const auto& attr = decoder_input_attrs_[input_index];
      if (attr.n_dims >= 3 && attr.dims[1] == 1) {
        decoder_mask_input_index_ = input_index;
      } else if (attr.n_dims >= 3 && attr.dims[1] > 1) {
        decoder_z_input_index_ = input_index;
      }
    }

    if (decoder_z_input_index_ == decoder_io_num_.n_input || decoder_mask_input_index_ == decoder_io_num_.n_input ||
        decoder_z_input_index_ == decoder_mask_input_index_) {
      throw std::runtime_error("Failed to identify RKNN Piper decoder z/y_mask inputs");
    }

    decoder_z_element_count_ = decoder_input_attrs_[decoder_z_input_index_].n_elems;
    decoder_mask_element_count_ = decoder_input_attrs_[decoder_mask_input_index_].n_elems;
    decoder_z_channel_count_ = static_cast<std::size_t>(decoder_input_attrs_[decoder_z_input_index_].dims[1]);
    if (decoder_z_element_count_ == 0 || decoder_mask_element_count_ == 0 ||
        decoder_z_channel_count_ <= 1 ||
        decoder_z_element_count_ != decoder_mask_element_count_ * decoder_z_channel_count_) {
      throw std::runtime_error("Unexpected RKNN Piper decoder input tensor shape");
    }

    if (decoder_output_attrs_[0].n_elems == 0) {
      throw std::runtime_error("RKNN Piper decoder audio output tensor has no elements");
    }
  }

  void PiperSynthesizer::allocate_decoder_workspaces() {
    decoder_z_input_.resize(decoder_z_element_count_);
    decoder_mask_input_.resize(decoder_mask_element_count_);
    decoder_audio_output_.resize(decoder_output_attrs_[0].n_elems);
    decoder_outputs_.resize(decoder_output_attrs_.size());

    decoder_outputs_[0] = {};
    decoder_outputs_[0].want_float = 1;
    decoder_outputs_[0].is_prealloc = 1;
    decoder_outputs_[0].index = 0;
    decoder_outputs_[0].buf = decoder_audio_output_.data();
    decoder_outputs_[0].size = static_cast<std::uint32_t>(decoder_audio_output_.size() * sizeof(float));
  }

  auto PiperSynthesizer::run_encoder(const std::vector<std::int64_t>& phoneme_ids)
      -> std::pair<std::vector<float>, std::vector<float>> {
    if (phoneme_ids.empty()) {
      throw std::runtime_error("Piper encoder received no phoneme ids");
    }

    auto input_lengths = std::vector<std::int64_t>{static_cast<std::int64_t>(phoneme_ids.size())};
    auto scales = std::vector<float>{phonemizer_.config().noise_scale, phonemizer_.config().length_scale,
                                     phonemizer_.config().noise_w};
    auto phoneme_shape = std::vector<std::int64_t>{1, static_cast<std::int64_t>(phoneme_ids.size())};
    auto length_shape = std::vector<std::int64_t>{1};
    auto scales_shape = std::vector<std::int64_t>{3};

    auto input_tensors = std::vector<Ort::Value>{};
    input_tensors.push_back(Ort::Value::CreateTensor<std::int64_t>(
        onnx_memory_info_, const_cast<std::int64_t*>(phoneme_ids.data()), phoneme_ids.size(), phoneme_shape.data(),
        phoneme_shape.size()));
    input_tensors.push_back(Ort::Value::CreateTensor<std::int64_t>(
        onnx_memory_info_, input_lengths.data(), input_lengths.size(), length_shape.data(), length_shape.size()));
    input_tensors.push_back(Ort::Value::CreateTensor<float>(onnx_memory_info_, scales.data(), scales.size(),
                                                            scales_shape.data(), scales_shape.size()));

    const std::array<const char*, 3> input_names = {"input", "input_lengths", "scales"};
    const std::array<const char*, 2> output_names = {"z", "y_mask"};
    auto output_tensors = encoder_session_.Run(Ort::RunOptions{nullptr}, input_names.data(), input_tensors.data(),
                                               input_tensors.size(), output_names.data(), output_names.size());

    if (output_tensors.size() != 2 || !output_tensors[0].IsTensor() || !output_tensors[1].IsTensor()) {
      throw std::runtime_error("Unexpected Piper encoder outputs");
    }

    const auto z_shape = output_tensors[0].GetTensorTypeAndShapeInfo().GetShape();
    const auto mask_shape = output_tensors[1].GetTensorTypeAndShapeInfo().GetShape();
    const auto z_length = tensor_last_dim(z_shape);
    const auto mask_length = tensor_last_dim(mask_shape);
    if (z_length != mask_length) {
      throw std::runtime_error("Piper encoder z/y_mask length mismatch");
    }

    const auto z_count = output_tensors[0].GetTensorTypeAndShapeInfo().GetElementCount();
    const auto mask_count = output_tensors[1].GetTensorTypeAndShapeInfo().GetElementCount();
    auto z = std::vector<float>(z_count);
    auto y_mask = std::vector<float>(mask_count);
    std::copy_n(output_tensors[0].GetTensorData<float>(), z.size(), z.begin());
    std::copy_n(output_tensors[1].GetTensorData<float>(), y_mask.size(), y_mask.begin());
    return {std::move(z), std::move(y_mask)};
  }

  auto PiperSynthesizer::run_decoder(const std::vector<float>& z, const std::vector<float>& y_mask)
      -> std::vector<float> {
    if (y_mask.empty() || decoder_z_channel_count_ == 0 ||
        z.size() != y_mask.size() * decoder_z_channel_count_) {
      throw std::runtime_error("Unexpected Piper encoder output sizes for RKNN decoder");
    }
    if (z.size() > decoder_z_input_.size() || y_mask.size() > decoder_mask_input_.size()) {
      throw std::runtime_error("Piper encoder output is longer than RKNN decoder static input capacity");
    }

    std::fill(decoder_z_input_.begin(), decoder_z_input_.end(), 0.0F);
    std::fill(decoder_mask_input_.begin(), decoder_mask_input_.end(), 0.0F);
    std::copy(z.begin(), z.end(), decoder_z_input_.begin());
    std::copy(y_mask.begin(), y_mask.end(), decoder_mask_input_.begin());

    auto inputs = std::array<rknn_input, 2>{};
    inputs[decoder_z_input_index_] = {};
    inputs[decoder_z_input_index_].index = decoder_z_input_index_;
    inputs[decoder_z_input_index_].buf = decoder_z_input_.data();
    inputs[decoder_z_input_index_].size = static_cast<std::uint32_t>(decoder_z_input_.size() * sizeof(float));
    inputs[decoder_z_input_index_].pass_through = 0;
    inputs[decoder_z_input_index_].type = RKNN_TENSOR_FLOAT32;
    inputs[decoder_z_input_index_].fmt = decoder_input_attrs_[decoder_z_input_index_].fmt;

    inputs[decoder_mask_input_index_] = {};
    inputs[decoder_mask_input_index_].index = decoder_mask_input_index_;
    inputs[decoder_mask_input_index_].buf = decoder_mask_input_.data();
    inputs[decoder_mask_input_index_].size = static_cast<std::uint32_t>(decoder_mask_input_.size() * sizeof(float));
    inputs[decoder_mask_input_index_].pass_through = 0;
    inputs[decoder_mask_input_index_].type = RKNN_TENSOR_FLOAT32;
    inputs[decoder_mask_input_index_].fmt = decoder_input_attrs_[decoder_mask_input_index_].fmt;

    auto result = rknn_inputs_set(decoder_context_, inputs.size(), inputs.data());
    if (result < 0) {
      throw rknn_error("Failed to set RKNN Piper decoder inputs", result);
    }

    result = rknn_run(decoder_context_, nullptr);
    if (result < 0) {
      throw rknn_error("Failed to run RKNN Piper decoder inference", result);
    }

    result = rknn_outputs_get(decoder_context_, decoder_io_num_.n_output, decoder_outputs_.data(), nullptr);
    if (result < 0) {
      throw rknn_error("Failed to get RKNN Piper decoder outputs", result);
    }
    RknnOutputReleaseGuard output_release_guard{decoder_context_, decoder_io_num_.n_output, decoder_outputs_.data()};

    auto audio = decoder_audio_output_;
    result = output_release_guard.release();
    if (result < 0) {
      throw rknn_error("Failed to release RKNN Piper decoder outputs", result);
    }

    const auto valid_mask_frames =
        static_cast<std::size_t>(std::count_if(y_mask.begin(), y_mask.end(), [](float value) { return value > 0.5F; }));
    if (decoder_mask_input_.empty()) {
      throw std::runtime_error("RKNN Piper decoder mask input workspace is empty");
    }
    const auto samples_per_mask_frame = decoder_audio_output_.size() / decoder_mask_input_.size();
    if (samples_per_mask_frame == 0) {
      throw std::runtime_error("RKNN Piper decoder audio output is shorter than mask input capacity");
    }

    const auto valid_sample_count = std::min(audio.size(), valid_mask_frames * samples_per_mask_frame);
    audio.resize(valid_sample_count);
    spdlog::debug("Piper decoder audio static_samples={} valid_mask_frames={} samples_per_mask_frame={} "
                  "trimmed_samples={}",
                  decoder_audio_output_.size(), valid_mask_frames, samples_per_mask_frame, audio.size());
    return audio;
  }

} // namespace signlang::speech_tts
