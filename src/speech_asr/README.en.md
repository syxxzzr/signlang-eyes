# Speech ASR

[简体中文](README.md) | [English](README.en.md)

Speech recognition module based on the OpenAI Whisper architecture, supporting Chinese and English speech-to-text. The encoder runs on ONNX Runtime (CPU) and the decoder runs on RKNN (NPU), using a sliding-window approach for continuous recognition.

## Features

- Subscribes to audio frames from iceoryx2 (16 kHz mono)
- Computes Mel spectrogram features (80-bin filter bank, FFT accelerated via FFTW3)
- Whisper encoder on ONNX Runtime, decoder on RK3588 NPU
- Publishes recognition results over iceoryx2 publish-subscribe
- Pauses inference when no downstream subscribers are connected

## Processing Pipeline

```text
iceoryx2 audio → AudioRingBuffer → Sliding-window extraction → Mel spectrogram (FFTW3)
                                                                   ↓
                                                           Whisper encoder (ONNX Runtime / CPU)
                                                                   ↓
                                                           Whisper decoder (RKNN / NPU)
                                                                   ↓
                                                           SpeechAsrResult → iceoryx2
```

## CLI Arguments

| Argument | Description |
| --- | --- |
| `-i, --input-service <name>` | iceoryx2 audio input service (required) |
| `-o, --output-service <name>` | iceoryx2 ASR result output service (required) |
| `--language <en|zh>` | Recognition language (default: `en`) |
| `--encoder-model <path>` | Whisper encoder ONNX model path |
| `--decoder-model <path>` | Whisper decoder RKNN model path |
| `--vocab-en <path>` | English vocabulary file |
| `--vocab-zh <path>` | Chinese vocabulary file |
| `--mel-filters <path>` | 80-bin Mel filter file |
| `-w, --window-ms <ms>` | ASR window length (default: 15000 ms) |
| `--overlap <ratio>` | Window overlap ratio (default: 0.1) |
| `--max-decode-steps <n>` | Maximum decoder iterations (default: 96) |
| `--subscriber-buffer <n>` | iceoryx2 subscriber queue size (default: 2) |
| `--npu-core <mask>` | Encoder NPU core mask |
| `--decoder-npu-core <mask>` | Decoder NPU core mask |
| `--rknn-priority <high|medium|low>` | NPU priority |
| `--cpu-core <n>` | CPU core to pin to |

## Architecture

Dual-thread design: a receiver thread pulls audio from iceoryx2 into a ring buffer, while a detector thread reads audio windows from the buffer and performs inference. The audio ring buffer, Mel filters, and encoder output buffers all use pre-allocated memory to avoid runtime allocations.

## Dependencies

- ONNX Runtime (encoder inference)
- RKNN API (decoder inference)
- FFTW3 (spectrogram computation)
- iceoryx2 (publish-subscribe)
