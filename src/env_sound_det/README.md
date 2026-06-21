# env_sound_det — Environmental Sound Detection (YAMNet)

## Overview

The **env_sound_det** module performs real-time environmental sound classification using Google's YAMNet (MobileNetV1-based) model running on the RKNN NPU. It subscribes to audio frames, processes them through a sliding window, runs inference, and requests alert-state changes for configured horn/dangerous-sound labels.

- **Executable**: `env_sound_det` (installed under `bin/`)
- **IPC Pattern**: Publish-Subscribe (audio subscriber) + Request-Response (state control)
- **Input**: `signlang::audio_frontend::AudioFrame` from iceoryx2
- **Output**: `DangerousSound` state-control requests when configured horn labels are detected
- **Model**: YAMNet (521-class environmental sound classifier, RKNN-accelerated)

## Command-Line Parameters

### IPC (Required)

| Parameter | Description |
|-----------|-------------|
| `--input-service` / `-i` | iceoryx2 audio input publish-subscribe service name |
| `--state-control-service` | iceoryx2 app state control request-response service name |

### Model Paths

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--model` / `-m` | `models/yamnet/yamnet_3s.rknn` | YAMNet RKNN model path |
| `--class-map` | `models/yamnet/yamnet_class_map.txt` | YAMNet 521-class label mapping file |

### Processing

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--window-ms` / `-w` | `3000` | Detection window duration in milliseconds (must be > 0) |
| `--overlap` | `0.2` | Overlap ratio between adjacent windows (0.0-1.0) |
| `--score-threshold` | `0.3` | Minimum score threshold for detection (0.0-1.0) |

### Performance

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--npu-core` | `auto` | NPU core mask: `auto`, `all`, `0`, `1`, `2`, `0_1`, `0_1_2` |
| `--rknn-priority` | `medium` | RKNN context priority: `high`, `medium`, `low` |
| `--subscriber-buffer` | `2` | iceoryx2 subscriber queue size (must be > 0) |

## Technical Details

### YAMNet Architecture

- **Backbone**: MobileNetV1 with depthwise-separable convolutions
- **Input**: `[1, 48000]` — 3 seconds of audio @ 16 kHz mono
- **Output**: `[N_frames × 521]` — per-frame class scores (N_frames varies with model)
- **Post-processing**: Average scores across all output frames, filter by threshold

### Threshold-Based Filtering

Instead of selecting a fixed top-K, the module uses **threshold-based filtering**:
- All classes with `score >= score_threshold` are detected (up to 32 max)
- Provides flexible detection: can find 0, 1, or many dangerous sounds
- More accurate for multi-class scenarios (e.g., car horn + siren simultaneously)
- Threshold is configurable per deployment scenario (default 0.3)

### Inference Pipeline

1. Accumulate samples in ring buffer (sliding window with configurable overlap)
2. If audio window matches model input length, pass directly; otherwise zero-pad tail
3. Run single-pass RKNN inference
4. Average per-class scores across all time frames
5. Filter classes by score threshold, collect up to 32 classes
6. Check detected classes against dangerous sound labels

### Event-Driven Subscription

The module uses iceoryx2 `Node::wait()` for event-driven audio frame reception:
- No periodic polling when idle
- Zero CPU usage while waiting for frames
- 5ms timeout for responsive shutdown
- Replaces legacy polling loops for better power efficiency

### State Control (Dangerous Sound Detection)

When the module detects one of the configured horn labels in the filtered classes, it requests a `DangerousSound` state change via the iceoryx2 Request-Response control service.

**Dangerous sound labels:**
- `Air horn, truck horn`
- `Vehicle horn, car horn, honking`
- `Train horn`

### YAMNet Window Strategy

- **Window Size**: 3 seconds (3000ms) at 16 kHz = 48,000 samples
- **Overlap**: 20% (0.2) → hop of 38,400 samples (2.4 seconds)
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
YamnetModel (RKNN NPU)
    │
    ├─► Inference (MobileNetV1)
    │       │
    │       ▼
    │   Per-frame Scores [N×521]
    │       │
    │       ▼
    │   Average across frames
    │       │
    │       ▼
    │   Mean Scores [521]
    │       │
    └─► Threshold Filter
            │
            ▼
        Detected Classes
        (score >= 0.3)
            │
            ▼
     Dangerous Sound Check
            │
            ▼
    State Control Client
     (iceoryx2 request)
```

## Dependencies

- **iceoryx2**: Zero-copy IPC
- **RKNN Runtime**: NPU inference
- **spdlog**: Logging

## Usage Examples

### Basic Usage

```bash
./env_sound_det \
    --input-service audio_capture \
    --state-control-service app_state_control
```

### Custom Threshold

```bash
# More sensitive detection (lower threshold)
./env_sound_det \
    --input-service audio_capture \
    --state-control-service app_state_control \
    --score-threshold 0.2
```

### Longer Window

```bash
# 5-second detection window
./env_sound_det \
    --input-service audio_capture \
    --state-control-service app_state_control \
    --window-ms 5000 \
    --overlap 0.3
```

### Specific NPU Core

```bash
./env_sound_det \
    --input-service audio_capture \
    --state-control-service app_state_control \
    --npu-core 0 \
    --score-threshold 0.3
```

## File Organization

| File | Description |
|------|-------------|
| `main.cpp` | Entry point; dual-thread orchestration (receiver + detector) |
| `program_options.{cpp,hpp}` | CLI argument parsing via cxxopts |
| `yamnet_model.{cpp,hpp}` | YAMNet RKNN model wrapper with threshold filtering |
| `iceoryx_gateway.{cpp,hpp}` | iceoryx2 subscriber and state control client |

Shared with other modules:
| File | Description |
|------|-------------|
| `common/audio_ring_buffer.{cpp,hpp}` | Thread-safe circular buffer for audio windows |

## IPC Data Structures

### YamnetInferenceResult (Internal)

```cpp
struct YamnetInferenceResult {
    std::uint32_t model_input_sample_count;
    std::uint32_t score_frame_count;
    std::uint32_t detected_class_count;  // Number of classes above threshold
    float inference_time_ms;
    std::array<EnvSoundClassScore, kMaxDetectedClasses> detected_classes;
};

struct EnvSoundClassScore {
    std::uint32_t class_index;
    float score;
    std::array<char, kMaxClassLabelLength> label;
};
```

## Performance Characteristics

- **Inference time**: ~20-40ms for 3s window on single NPU core (RK3588)
- **Throughput**: Real-time factor ~0.01-0.02 (processes 3s in 20-40ms)
- **Memory**: ~8MB model footprint
- **CPU usage**: <3% on single core (event-driven, no polling)
- **Latency**: ~3s glass-to-glass (3s window with 20% overlap)
- **Detection flexibility**: 0-32 classes per window (threshold-based)

## YAMNet Class Examples

| ID | Label |
|----|-------|
| 0 | Speech |
| 6 | Shout |
| 292 | Fire |
| 302 | Vehicle horn, car horn, honking |
| 304 | Car alarm |
| 312 | Air horn, truck horn |
| 316 | Emergency vehicle |
| 325 | Train horn |
| 421 | Gunshot, gunfire |

See `models/yamnet/yamnet_class_map.txt` for all 521 classes.

## Threshold Tuning Guidelines

| Threshold | Sensitivity | Use Case |
|-----------|-------------|----------|
| 0.1-0.2 | Very high | Noisy environments, need early warning |
| 0.3 (default) | Balanced | General purpose, good precision/recall |
| 0.4-0.5 | Conservative | Low false positive tolerance |
| 0.6+ | Very conservative | Only very confident detections |

## Troubleshooting

### No Audio Received

Check that audio_frontend is running and publishing to the correct service:
```bash
# Should show audio_capture service
iox2-list
```

### False Positives

Increase the score threshold:
```bash
./env_sound_det --score-threshold 0.4
```

### Missed Detections

Lower the score threshold or increase window size:
```bash
./env_sound_det --score-threshold 0.2 --window-ms 5000
```

### RKNN Initialization Failed

Ensure RKNN models are present and `librknnrt.so` is in library path:
```bash
ls models/yamnet/
export LD_LIBRARY_PATH=/path/to/lib:$LD_LIBRARY_PATH
```
