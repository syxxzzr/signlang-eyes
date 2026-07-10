# Speech TTS

[简体中文](README.md) | [English](README.en.md)

Chinese speech synthesis module. Receives text via an iceoryx2 request-response interface, synthesizes speech using Piper models (ONNX encoder + RKNN decoder) with cpp-pinyin phonemization, and plays the output through ALSA.

## Features

- Receives TTS requests from `dataflow_dispatcher` over iceoryx2 request-response
- Converts Chinese text to pinyin sequences via cpp-pinyin
- Piper encoder on ONNX Runtime, decoder on RK3588 NPU
- Plays synthesized audio through an ALSA PCM device
- Supports graceful cancellation via generation counting, ensuring new requests automatically supersede in-progress synthesis

## Processing Pipeline

```text
iceoryx2 TTS request → SpeechTtsService (task queue / cancellation)
                           ↓
                     Piper phonemization (cpp-pinyin)
                           ↓
                     Piper encoder (ONNX Runtime / CPU)
                           ↓
                     Piper decoder (RKNN / NPU)
                           ↓
                     ALSA playback device
```

## CLI Arguments

| Argument | Description |
| --- | --- |
| `-s, --service <name>` | iceoryx2 TTS request-response service name (required) |
| `-d, --device <name>` | ALSA playback device (default: `default`) |
| `--encoder-model <path>` | Piper encoder ONNX model path |
| `--decoder-model <path>` | Piper decoder RKNN model path |
| `--config <path>` | Piper model JSON configuration file |
| `--pinyin-dict <dir>` | cpp-pinyin dictionary directory |
| `--npu-core <mask>` | NPU core mask |
| `--rknn-priority <high|medium|low>` | NPU priority |
| `--cpu-core <n>` | CPU core to pin to |

## Concurrency & Cancellation

`SpeechTtsService` uses a generation counter for preemptive cancellation: each new request increments the generation, and the synthesis worker thread checks whether its generation is still current before encoding, decoding, and ALSA write-out. This avoids audible overlap from stale requests.

## Dependencies

- ONNX Runtime (encoder inference)
- RKNN API (decoder inference)
- cpp-pinyin (Chinese phonemization)
- ALSA (audio playback)
- iceoryx2 (request-response)
