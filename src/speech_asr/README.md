# speech_asr — Speech Recognition (Whisper)

## Overview

The **speech_asr** module performs real-time speech-to-text recognition using an OpenAI Whisper base model running on the RKNN NPU. It subscribes to audio frames, processes them through a sliding-window log-Mel spectrogram pipeline, runs encoder-decoder inference, and publishes transcription results. Supports English and Chinese languages with configurable state-based enable/disable control.

- **Executable**: `speech_asr` (installed under `bin/`)
- **IPC Pattern**: Publish-Subscribe (subscriber + publisher) + Event/Blackboard (state control)
- **Input**: `signlang::audio_frontend::AudioFrame` from iceoryx2
- **Output**: `signlang::speech_asr::SpeechAsrResult` on iceoryx2
- **Model**: Whisper base (encoder + decoder, RKNN-accelerated)

## File Inventory

| File | Description |
|------|-------------|
| `main.cpp` | Entry point; dual-thread orchestration (receiver + inference thread) |
| `program_options.{cpp,hpp}` | CLI argument parsing via cxxopts |
| `whisper_model.{cpp,hpp}` | Whisper encoder/decoder RKNN model: Mel spectrogram computation, encoder inference, autoregressive decoder |
| `iceoryx_gateway.{cpp,hpp}` | iceoryx2 subscriber (audio), publisher (results), event/blackboard (state control) |
| `audio_ring_buffer.{cpp,hpp}` | Thread-safe circular buffer for audio window accumulation |
| `speech_asr_result.{cpp,hpp}` | `SpeechAsrResult` IPC message definition |

## Command-Line Parameters

### IPC (Required)

| Parameter | Description |
|-----------|-------------|
| `--input-service` / `-i` | iceoryx2 audio input publish-subscribe service name |
| `--output-service` / `-o` | iceoryx2 ASR result output service name |
| `--state-event-service` | iceoryx2 event service name for global app state change notifications |
| `--state-blackboard-service` | iceoryx2 blackboard service name for global app state storage |

### Model Paths

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--encoder-model` | `models/whisper/whisper_encoder_base_15s.rknn` | Whisper encoder RKNN model path |
| `--decoder-model` | `models/whisper/whisper_decoder_base_15s.rknn` | Whisper decoder RKNN model path |
| `--vocab-en` | `models/whisper/vocab_en.txt` | English vocabulary file |
| `--vocab-zh` | `models/whisper/vocab_zh.txt` | Chinese vocabulary file (base64-encoded) |
| `--mel-filters` | `models/whisper/mel_80_filters.txt` | 80-bin Mel filterbank coefficients |

### Processing

| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| `--language` | `en` | `en`, `zh` | Recognition language |
| `--window-ms` / `-w` | `15000` | `1000–60000` | ASR analysis window duration in milliseconds |
| `--overlap` | `0.2` | `[0.0, 1.0)` | Overlap ratio between adjacent windows |
| `--max-decode-steps` | `96` | `1–1000` | Maximum autoregressive decoder iterations per window |

### Performance

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--npu-core` | `auto` | Default NPU core mask for both encoder and decoder: `auto`, `all`, `0`, `1`, `2`, `0_1`, `0_1_2` |
| `--encoder-npu-core` | *(from `--npu-core`)* | Encoder-specific NPU core mask (overrides `--npu-core`) |
| `--decoder-npu-core` | *(from `--npu-core`)* | Decoder-specific NPU core mask (overrides `--npu-core`) |
| `--rknn-priority` | `medium` | RKNN context priority: `high`, `medium`, `low` |
| `--poll-ms` | `2` | `1–100` | Subscriber polling sleep in ms when no sample is ready |
| `--subscriber-buffer` | `4` | `≥1` | iceoryx2 subscriber queue size |

## Technical Details

### Signal Processing Pipeline

1. **Reflect Padding**: ±200 samples at each edge of the audio window
2. **STFT**: 400-point FFT with Hann window, 160-sample hop length → 201 frequency bins
3. **Mel Filtering**: 80-bin Mel filterbank maps 201-bin power spectra → Mel scale
4. **Log Compression & Normalization**: `log10(mel_value)`, threshold at max − 8 dB, scale to [−1, 1]
5. **Encoder**: Single-pass encoding of the Mel spectrogram → encoded feature vector
6. **Decoder**: Autoregressive token generation using prompt tokens `<|startoftranscript|> <language> <|transcribe|> <|notimestamps|>`, terminates on `<|endoftext|>` or max steps

### Thread Architecture

- **Receiver Thread**: Subscribes to audio, accumulates samples in ring buffer
- **Inference Thread**: Waits for full window → Mel computation → encoder → decoder → publish result

### State Control

The module uses iceoryx2 Event + Blackboard pattern for enable/disable control:
- When **disabled**: Polls for state changes and sleeps briefly between checks
- When **enabled**: Non-blocking event check before each inference cycle
- Language is set at startup via `--language` flag

## Usage Example

```bash
./speech_asr \
    --input-service audio_capture \
    --output-service speech_asr_result \
    --state-event-service app_state_event \
    --state-blackboard-service app_state_blackboard \
    --language en \
    --window-ms 15000 \
    --overlap 0.2 \
    --max-decode-steps 96
```
