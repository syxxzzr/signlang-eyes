# speech_asr — Speech Recognition (Whisper)

## Overview

The **speech_asr** module performs real-time speech-to-text recognition using an OpenAI Whisper base encoder-decoder model running on the RKNN NPU. It subscribes to audio frames, processes them through a sliding-window log-Mel spectrogram pipeline, runs encoder-decoder inference, and publishes transcription results. Supports English and Chinese languages.

- **Executable**: `speech_asr` (installed under `bin/`)
- **IPC Pattern**: Publish-Subscribe (audio subscriber + result publisher)
- **Input**: `signlang::audio_frontend::AudioFrame` from iceoryx2
- **Output**: `signlang::speech_asr::SpeechAsrResult` on iceoryx2
- **Model**: Whisper base (encoder + decoder, 15s window, RKNN-accelerated)

## Command-Line Parameters

Relative paths are resolved from the installation root. For installed module executables under `bin/`, the runtime root is the parent directory, so defaults like `models/…`, `conf/…`, and `log/…` do not depend on the shell current working directory.

All module executables also accept `--log-file <path>` and `--log-rotate-size <bytes>`; the launcher supplies these automatically when it starts modules.

### IPC (Required)

| Parameter | Description |
|-----------|-------------|
| `--input-service` / `-i` | iceoryx2 audio input publish-subscribe service name |
| `--output-service` / `-o` | iceoryx2 ASR result output service name |

### Model Paths

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--encoder-model` | `models/whisper/whisper_encoder_base_15s.rknn` | Whisper encoder RKNN model path |
| `--decoder-model` | `models/whisper/whisper_decoder_base_15s.rknn` | Whisper decoder RKNN model path |
| `--vocab-en` | `models/whisper/vocab_en.txt` | English vocabulary file |
| `--vocab-zh` | `models/whisper/vocab_zh.txt` | Chinese vocabulary file (base64-encoded) |
| `--mel-filters` | `models/whisper/mel_80_filters.txt` | 80-bin Mel filterbank coefficients |

### Processing

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--language` | `en` | Recognition language: `en` or `zh` |
| `--window-ms` / `-w` | `15000` | ASR analysis window duration in milliseconds (must be > 0) |
| `--overlap` | `0.2` | Overlap ratio between adjacent windows (0.0-1.0) |
| `--max-decode-steps` | `96` | Maximum autoregressive decoder iterations per window (must be > 0) |

### Performance

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--npu-core` | `auto` | Default NPU core mask: `auto`, `all`, `0`, `1`, `2`, `0_1`, `0_1_2` |
| `--encoder-npu-core` | *(from `--npu-core`)* | Encoder-specific NPU core mask (overrides `--npu-core`) |
| `--decoder-npu-core` | *(from `--npu-core`)* | Decoder-specific NPU core mask (overrides `--npu-core`) |
| `--rknn-priority` | `medium` | RKNN context priority: `high`, `medium`, `low` |
| `--subscriber-buffer` | `2` | iceoryx2 subscriber queue size (must be > 0) |

## Technical Details

### Signal Processing Pipeline

1. **Reflect Padding**: ±200 samples at each edge of the audio window (prevents edge artifacts)
2. **STFT**: 400-point FFT with Hann window, 160-sample hop length → 201 frequency bins (FFTW3f)
3. **Mel Filtering**: 80-bin Mel filterbank maps 201-bin power spectra to perceptual Mel scale
4. **Log Compression & Normalization**: `log10(mel_value)`, threshold at max − 8 dB, scale to [−1, 1]
5. **Encoder**: Single-pass RKNN encoding of the Mel spectrogram → fixed-length feature vector
6. **Decoder**: Autoregressive token generation with prompt `<|startoftranscript|> <language> <|transcribe|> <|notimestamps|>`, terminates on `<|endoftext|>` or max steps

**Window dimensions**: 15s @ 16kHz = 240,000 samples → 80×1500 Mel spectrogram

### Thread Architecture

- **Receiver Thread**: Event-driven subscription via `Node::wait()`, accumulates samples in ring buffer (zero CPU when idle)
- **Inference Thread**: Waits for full window → Mel computation → encoder → decoder → publish result

### Event-Driven Subscription

The module uses iceoryx2 `Node::wait()` for event-driven audio frame reception:
- No periodic polling when idle
- Zero CPU usage while waiting for frames
- 5ms timeout for responsive shutdown
- Replaces legacy polling loops for better power efficiency

### ASR Window Strategy

- **Window Size**: 15 seconds (15000ms) at 16 kHz = 240,000 samples
- **Overlap**: 20% (0.2) → hop of 192,000 samples (12 seconds)
- **Ring Buffer**: Maintains `window + max(window, hop) + 1s` capacity for continuous streaming

## Architecture

```
Audio Subscriber (iceoryx2)
    │ [event-driven via Node::wait()]
    ▼
AudioRingBuffer
(circular buffer)
    │
    ▼
WhisperModel
    │
    ├─► STFT + Mel Spectrogram (FFTW3f)
    │
    ├─► Encoder (RKNN NPU)
    │       │
    │       ▼
    │   Encoded Features
    │       │
    └─► Decoder (RKNN NPU, autoregressive)
            │
            ▼
        Token IDs
            │
            ▼
        Vocabulary Lookup
            │
            ▼
    Result Publisher (iceoryx2)
```

## Dependencies

- **iceoryx2**: Zero-copy IPC
- **RKNN Runtime**: NPU inference
- **FFTW3f**: Fast Fourier Transform
- **spdlog**: Logging

## Usage Examples

### Basic Usage (English)

```bash
install/bin/speech_asr \
    --input-service audio_capture \
    --output-service speech_asr_result \
    --language en
```

### Chinese Recognition

```bash
install/bin/speech_asr \
    --input-service audio_capture \
    --output-service speech_asr_result \
    --language zh \
    --window-ms 15000 \
    --overlap 0.2
```

### Multi-Core NPU

```bash
# Use NPU cores 0 and 1
install/bin/speech_asr \
    --input-service audio_capture \
    --output-service speech_asr_result \
    --language en \
    --npu-core 0_1
```

### Separate Encoder/Decoder Cores

```bash
# Encoder on core 0, decoder on core 1
install/bin/speech_asr \
    --input-service audio_capture \
    --output-service speech_asr_result \
    --language en \
    --encoder-npu-core 0 \
    --decoder-npu-core 1
```

## File Organization

| File | Description |
|------|-------------|
| `main.cpp` | Entry point; dual-thread orchestration (receiver + inference) |
| `program_options.{cpp,hpp}` | CLI argument parsing via cxxopts |
| `whisper_model.{cpp,hpp}` | Whisper encoder/decoder RKNN model wrapper |
| `iceoryx_gateway.{cpp,hpp}` | iceoryx2 subscriber and publisher |
| `speech_asr_result.{cpp,hpp}` | `SpeechAsrResult` IPC message definition |

Shared with other modules:
| File | Description |
|------|-------------|
| `common/audio_ring_buffer.{cpp,hpp}` | Thread-safe circular buffer for audio windows |

## IPC Data Structures

### SpeechAsrResult (Published)

```cpp
struct SpeechAsrResult {
    std::uint64_t timestamp_ns;
    std::uint64_t window_start_sequence;
    std::uint64_t window_end_sequence;
    std::uint32_t window_ms;
    float overlap_ratio;
    std::uint32_t token_count;
    float inference_time_ms;
    std::array<char, kMaxTranscriptLength> transcript;
};
```

## Performance Characteristics

- **Inference time**: ~1.5-2.5s for 15s window on single NPU core (RK3588)
- **Throughput**: Real-time factor ~0.15-0.20 (processes 15s in 1.5-2.5s)
- **Memory**: ~150MB model footprint (encoder + decoder RKNN models)
- **CPU usage**: <5% on single core (event-driven, no polling; Cortex-A76 @ 2.4GHz)
- **Latency**: ~2-3s glass-to-glass (15s window with 20% overlap = 12s hop)
- **STFT computation**: ~50-100ms for 240k samples via FFTW3f

## Vocabulary Files

- **vocab_en.txt**: 50,257 tokens (plain text, one per line)
- **vocab_zh.txt**: 50,257 tokens (base64-encoded UTF-8 for Chinese characters)
- **mel_filters.txt**: 80×201 Mel filterbank matrix (text format)

## Troubleshooting

### No Audio Received

Check that audio_frontend is running and publishing to the correct service:
```bash
# Should show audio_capture service
iox2-list
```

### RKNN Initialization Failed

Ensure RKNN models are present and `librknnrt.so` is in library path:
```bash
ls models/whisper/
export LD_LIBRARY_PATH=/path/to/lib:$LD_LIBRARY_PATH
```

### Poor Recognition Quality

- Check input audio sample rate matches expected 16 kHz
- Increase window size for longer utterances
- Verify vocabulary file matches the selected language
