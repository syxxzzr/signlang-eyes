# Environmental Sound Detection

[简体中文](README.md) | [English](README.en.md)

Environmental sound recognition module. Runs YAMNet on the RK3588 NPU to classify environmental sounds, detecting dangerous sounds (e.g., vehicle horns) and triggering the application danger state.

## Features

- Subscribes to audio frames from iceoryx2 (16 kHz mono)
- Runs YAMNet inference on the RKNN NPU
- Detects predefined dangerous sound classes: "Air horn, truck horn", "Vehicle horn, car horn, honking", "Train horn"
- Triggers the `DangerousSound` state via the iceoryx2 state control interface upon detection
- Uses a sliding-window approach for continuous detection (default window: 3 seconds)

## Processing Pipeline

```text
iceoryx2 audio → AudioRingBuffer → Sliding-window extraction → YAMNet (RKNN / NPU)
                                                                   ↓
                                                            Danger label match → state_control.EnterSpecial(DangerousSound)
```

## CLI Arguments

| Argument | Description |
| --- | --- |
| `-i, --input-service <name>` | iceoryx2 audio input service (required) |
| `--state-control-service <name>` | iceoryx2 state control service (required) |
| `-m, --model <path>` | YAMNet RKNN model path |
| `--class-map <path>` | YAMNet class label file |
| `-w, --window-ms <ms>` | Detection window length (default: 3000 ms) |
| `--overlap <ratio>` | Window overlap ratio (default: 0.2) |
| `--score-threshold <0.0-1.0>` | Detection confidence threshold (default: 0.3) |
| `--subscriber-buffer <n>` | iceoryx2 subscriber queue size (default: 2) |
| `--npu-core <mask>` | NPU core mask |
| `--rknn-priority <high|medium|low>` | NPU priority |

## Design Notes

- YAMNet expects 16 kHz mono input; the module verifies compatibility with the `audio_frontend` publish format
- Dual-thread architecture: a receiver thread buffers audio, a detector thread runs inference
- Sample rate mismatches are detected and warned about to prevent buffer starvation
- Each window is classified independently; `score_threshold` adjusts detection sensitivity

## Dependencies

- RKNN API (inference)
- iceoryx2 (publish-subscribe, request-response)
