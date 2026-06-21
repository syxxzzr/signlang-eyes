# signlang_det — Sign Language Recognition (BiLSTM + DTW)

## Overview

The **signlang_det** module performs real-time dual-hand sign language recognition using a hybrid BiLSTM + DTW (Dynamic Time Warping) architecture. It subscribes to hand pose detection results, extracts 126-dimensional spatial-temporal features, encodes them with a BiLSTM on the RKNN NPU, and matches the resulting embeddings against pre-recorded gesture prototypes using DTW for speed-invariant recognition.

- **Executable**: `signlang_det` (installed under `bin/`)
- **IPC Pattern**: Publish-Subscribe (subscriber + publisher) + Event/Blackboard (state control)
- **Input**: `iox2::bb::Slice<signlang::handpose_det::HandPoseDetection>` with `HandPoseFrameMetadata` user header from iceoryx2
- **Output**: `signlang::signlang_det::SignlangResult` on iceoryx2
- **Model**: BiLSTM encoder (RKNN NPU) + DTW matching (CPU)

## Command-Line Parameters

### IPC (Required)

| Parameter | Description |
|-----------|-------------|
| `--input-service` / `-i` | iceoryx2 handpose detection input service name |
| `--output-service` / `-o` | iceoryx2 sign language recognition result output service name |

### State Gate (Optional)

| Parameter | Description |
|-----------|-------------|
| `--state-event-service` | iceoryx2 event service for global app state change notifications |
| `--state-blackboard-service` | iceoryx2 blackboard service for global app state storage |

When both state gate services are provided, sign language recognition reads the current blackboard state at startup and follows subsequent app state events. Without state gate services, it stays enabled.

### Model Paths

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--model` / `-m` | `models/signlang/signlang.rknn` | BiLSTM encoder RKNN model path |
| `--label-map` | `models/signlang/labels.txt` | Gesture label mapping file (one label per line) |
| `--prototypes` | `models/signlang/prototypes.bin` | Encoded gesture prototype database (binary format) |

### Feature & Window

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--sequence-length` | `30` | Sliding window size in frames (must be > 0) |
| `--overlap-ratio` | `0.2` | Overlap ratio between adjacent sliding windows (0.0-1.0) |
| `--min-confidence` | `0.3` | Minimum hand keypoint detection confidence (0.0-1.0) |

### Feature Weighting

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--motion-weight` | `0.0` | Velocity (motion) feature weight: 0.0 = position-only, 1.0 = full motion (0.0-1.0) |

### DTW Matching

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--dtw-window-ratio` | `0.5` | Sakoe-Chiba DTW window ratio: larger = more speed-variation tolerance (0.0-1.0) |
| `--confidence-threshold` | `0.6` | Minimum recognition confidence for valid results (0.0-1.0) |
| `--confidence-margin` | `0.1` | Minimum gap between top-1 and top-2 confidence (0.0-1.0) |

### Performance

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--npu-core` | `auto` | NPU core mask: `auto`, `0`, `1`, `2`, `0_1`, `0_1_2`, `all` |
| `--subscriber-buffer` | `2` | iceoryx2 subscriber queue size (must be > 0) |

## Technical Details

### Feature Extraction (126-Dimensional)

Each frame produces a 126-dim feature vector (2 hands × 21 keypoints × 3 channels):

| Channel | Formula | Description |
|---------|---------|-------------|
| `normalized_x` | `(kp.x − wrist.x) / scale` | X-coordinate relative to wrist, normalized by max wrist-relative distance |
| `normalized_y` | `(kp.y − wrist.y) / scale` | Y-coordinate relative to wrist, same normalization scale |
| `velocity_magnitude` | `‖(x_t, y_t) − (x_{t−1}, y_{t−1})‖ × motion_weight` | Frame-to-frame motion speed, scaled by motion weight |

### Hand Tracking

- **Left-to-Right Ordering**: Hands sorted by x-coordinate per frame (`hands[0]` = left, `hands[1]` = right)
- **Slot-Based Tracking**: Hands matched to previous-frame slots by center-point distance
- **Occlusion Handling**: Zero-padding for missing hand slots (`present = false`)
- **Confidence Filtering**: Hands below `min_confidence` are zero-padded

### BiLSTM Encoder

- **Input**: `[1, 30, 126]` — batch × sequence_length × feature_dim
- **Output**: `[1, 30, 128]` — batch × sequence_length × frame_embedding_dim
- **Runtime**: RKNN NPU (single forward pass, ~10-20ms)
- **Architecture**: Bidirectional LSTM with 128 hidden units

### DTW Matching

1. **Distance Metric**: Normalized Euclidean distance per frame pair: `√(∑(q_i − s_j)² / dim)`
2. **Sakoe-Chiba Window**: `max(|Q|−|S|, round(max_len × window_ratio), 1)` — constrains alignment path
3. **Path Normalization**: Accumulated DTW cost ÷ number of alignment steps
4. **Confidence**: `1 / (1 + normalized_dtw_distance)` — distance-to-confidence conversion

### Dual-Threshold Filtering

1. **Confidence Threshold**: Reject if `top1_confidence < confidence_threshold`
2. **Margin Filter**: Reject if `top1_confidence − top2_confidence < confidence_margin` (prevents ambiguous gestures)

### Event-Driven Subscription

The module uses iceoryx2 `Node::wait()` for event-driven handpose frame reception:
- No periodic polling when idle
- Zero CPU usage while waiting for frames
- 5ms timeout for responsive shutdown when empty
- Replaces legacy polling loops for better power efficiency

### Thread Architecture

- **Receiver Thread**: Event-driven subscription → feature extraction → ring buffer push
- **Inference Thread**: Waits for full window → flatten features → BiLSTM encode → DTW match → filter → publish

### Sliding Window Strategy

- **Window Size**: 30 frames (1 second at 30 fps)
- **Overlap**: 20% (0.2) → hop of 24 frames (0.8 seconds)
- **Ring Buffer**: Maintains `sequence_length + max(sequence_length, hop_length)` capacity

## Architecture

```
Handpose Subscriber (iceoryx2)
    │ [event-driven via Node::wait()]
    ▼
State Gate (optional)
    │
    ▼ [enabled]
FeatureExtractor
    │
    ├─► Hand Tracking (L→R ordering)
    │       │
    │       ▼
    │   Coordinate Normalization
    │       │
    │       ▼
    │   Velocity Computation
    │       │
    └─► 126-dim Feature Vector
            │
            ▼
    KeypointRingBuffer
    (sliding window)
            │
            ▼
    SignlangModel
            │
            ├─► BiLSTM Encoder (RKNN NPU)
            │       │
            │       ▼
            │   Frame Embeddings [30×128]
            │       │
            └─► DTW Matcher (CPU)
                    │
                    ├─► Prototype Database
                    │   (pre-recorded gestures)
                    │
                    ▼
                DTW Distances
                    │
                    ▼
                Confidence Ranking
                    │
                    ▼
                Dual-Threshold Filter
                    │
                    ▼
            Result Publisher (iceoryx2)
```

## Dependencies

- **RKNN Runtime**: NPU inference for BiLSTM encoder
- **iceoryx2**: Zero-copy IPC
- **spdlog**: Logging

## Usage Examples

### Basic Usage

```bash
./signlang_det \
    --input-service handpose_result \
    --output-service signlang_result
```

### With State Control

```bash
./signlang_det \
    --input-service handpose_result \
    --output-service signlang_result \
    --state-event-service app_state_event \
    --state-blackboard-service app_state_blackboard
```

### Custom Recognition Parameters

```bash
# Longer window, stricter confidence
./signlang_det \
    --input-service handpose_result \
    --output-service signlang_result \
    --sequence-length 45 \
    --overlap-ratio 0.3 \
    --confidence-threshold 0.7 \
    --confidence-margin 0.15
```

### With Motion Features

```bash
# Include velocity in recognition (0.3 = 30% motion weight)
./signlang_det \
    --input-service handpose_result \
    --output-service signlang_result \
    --motion-weight 0.3 \
    --dtw-window-ratio 0.6
```

### Specific NPU Core

```bash
./signlang_det \
    --input-service handpose_result \
    --output-service signlang_result \
    --npu-core 0
```

## File Organization

| File | Description |
|------|-------------|
| `main.cpp` | Entry point; dual-thread orchestration (receiver + inference) |
| `program_options.{cpp,hpp}` | CLI argument parsing via cxxopts |
| `signlang_model.{cpp,hpp}` | BiLSTM RKNN model + DTW matching engine |
| `feature_extractor.{cpp,hpp}` | Hand tracking and 126-dim feature extraction |
| `keypoint_ring_buffer.{cpp,hpp}` | Thread-safe circular buffer for sliding windows |
| `iceoryx_gateway.{cpp,hpp}` | iceoryx2 subscriber, publisher, state control |
| `signlang_result.{cpp,hpp}` | IPC message definitions |

## IPC Data Structures

### SignlangResult (Published)

```cpp
struct SignlangResult {
    std::uint64_t timestamp_ns;
    std::uint64_t window_start_sequence;
    std::uint64_t window_end_sequence;
    std::uint32_t sequence_length;
    float overlap_ratio;
    float inference_time_ms;
    bool recognized;
    std::uint32_t gesture_id;
    float confidence;
    std::array<char, kMaxGestureLabelLength> label;
    std::uint32_t candidate_count;
    std::array<GestureCandidate, kMaxCandidates> candidates;
};

struct GestureCandidate {
    std::uint32_t gesture_id;
    float confidence;
    std::array<char, kMaxGestureLabelLength> label;
};
```

## Performance Characteristics

- **BiLSTM inference time**: ~10-20ms for 30-frame window on single NPU core (RK3588)
- **DTW matching time**: ~5-15ms per prototype (CPU, depends on prototype count)
- **Total latency**: ~50-100ms per recognition (30-frame window at 30 fps)
- **Memory**: ~12MB model footprint + prototype database
- **CPU usage**: <5% on single core (event-driven, no polling)
- **Recognition rate**: ~10-12 recognitions per second with 20% overlap

## Recognition Tuning Guidelines

### Sequence Length

| Length | Window Duration (30fps) | Use Case |
|--------|-------------------------|----------|
| 15-20 | 0.5-0.7s | Fast, simple gestures |
| 30 (default) | 1.0s | Balanced, most gestures |
| 45-60 | 1.5-2.0s | Complex, slow gestures |

### Confidence Threshold

| Threshold | Behavior | Use Case |
|-----------|----------|----------|
| 0.4-0.5 | Sensitive | More recognitions, higher false positive rate |
| 0.6 (default) | Balanced | Good precision/recall tradeoff |
| 0.7-0.8 | Conservative | Fewer false positives, may miss some gestures |

### Confidence Margin

| Margin | Behavior | Use Case |
|--------|----------|----------|
| 0.05-0.1 | Permissive | Allow similar gestures to be recognized |
| 0.1 (default) | Balanced | Good disambiguation |
| 0.15-0.2 | Strict | Only recognize when top-1 is clearly dominant |

### DTW Window Ratio

| Ratio | Behavior | Use Case |
|-------|----------|----------|
| 0.0-0.3 | Strict alignment | Speed-sensitive gestures |
| 0.5 (default) | Moderate tolerance | General purpose, allows some speed variation |
| 0.7-1.0 | Loose alignment | Very speed-invariant, slower matching |

### Motion Weight

| Weight | Behavior | Use Case |
|--------|----------|----------|
| 0.0 (default) | Position-only | Static pose-based gestures |
| 0.2-0.5 | Mixed | Gestures with some motion component |
| 0.6-1.0 | Motion-heavy | Dynamic, speed-based gestures |

## Troubleshooting

### No Handpose Data Received

Check that handpose_det is running and publishing to the correct service:
```bash
# Should show handpose_result service
iox2-list
```

### Low Recognition Rate

- Lower confidence threshold: `--confidence-threshold 0.5`
- Increase DTW window ratio: `--dtw-window-ratio 0.7`
- Check keypoint confidence in handpose_det output

### Too Many False Positives

- Increase confidence threshold: `--confidence-threshold 0.7`
- Increase confidence margin: `--confidence-margin 0.15`
- Reduce DTW window ratio: `--dtw-window-ratio 0.3`

### Gesture Not Recognized

- Check if gesture is in label map: `cat models/signlang/labels.txt`
- Verify prototype exists in database
- Try longer sequence length: `--sequence-length 45`

### RKNN Initialization Failed

Ensure RKNN model is present and `librknnrt.so` is in library path:
```bash
ls models/signlang/
export LD_LIBRARY_PATH=/path/to/lib:$LD_LIBRARY_PATH
```

### Module Not Processing Frames

Check if state machine is in the correct state:
- Sign language recognition only runs in SignLanguageChat or SignLanguageAi states
- Check state machine logs or trigger state change
