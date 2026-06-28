# signlang_det — Sign Language Recognition (BiLSTM + DTW)

## Overview

The **signlang_det** module performs real-time dual-hand sign language recognition using a hybrid BiLSTM + DTW (Dynamic Time Warping) architecture. It subscribes to hand pose detection results, extracts 168-dimensional spatial-temporal features, encodes them with a BiLSTM on the RKNN NPU, and matches the resulting embeddings against pre-recorded gesture prototypes stored in an SQLite database using DTW for speed-invariant recognition.

- **Executable**: `signlang_det` (installed under `bin/`)
- **IPC Pattern**: Publish-Subscribe (handpose subscriber + result publisher)
- **Input**: `iox2::bb::Slice<signlang::handpose_det::HandPoseDetection>` with `HandPoseFrameMetadata` user header from iceoryx2
- **Output**: `signlang::signlang_det::SignlangResult` on iceoryx2
- **Model**: BiLSTM encoder (128-dim embeddings, RKNN NPU) + DTW matching (CPU) against SQLite prototype database

## Command-Line Parameters

Relative paths are resolved from the installation root. For installed module executables under `bin/`, the runtime root is the parent directory, so defaults like `models/…`, `conf/…`, and `log/…` do not depend on the shell current working directory.

All module executables also accept `--log-file <path>` and `--log-rotate-size <bytes>`; the launcher supplies these automatically when it starts modules.

### IPC (Required)

| Parameter | Description |
|-----------|-------------|
| `--input-service` / `-i` | iceoryx2 handpose detection input service name |
| `--output-service` / `-o` | iceoryx2 sign language recognition result output service name |

### Model Paths

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--model` / `-m` | `models/bilstm/biltsm.rknn` | BiLSTM encoder RKNN model path |
| `--prototypes` | `conf/prototypes.sqlite` | SQLite gesture vocabulary and encoded prototype database |

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
| `--duplicate-suppression-ms` | `1000` | Suppress publishing the same recognized gesture again within this many milliseconds (`0` disables) |

### Performance

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--npu-core` | `auto` | NPU core mask: `auto`, `0`, `1`, `2`, `0_1`, `0_1_2`, `all` |
| `--subscriber-buffer` | `2` | iceoryx2 subscriber queue size (must be > 0) |

## Technical Details

### Feature Extraction (168-Dimensional)

Each frame produces a 168-dim feature vector (2 hands × 21 keypoints × 4 channels):

| Channel | Formula | Description |
|---------|---------|-------------|
| `normalized_x` | `(kp.x − wrist.x) / scale` | X-coordinate relative to wrist, normalized by max wrist-relative distance across hand |
| `normalized_y` | `(kp.y − wrist.y) / scale` | Y-coordinate relative to wrist, same normalization scale |
| `normalized_z` | `(kp.z − wrist.z) / scale` | Z-coordinate relative to wrist, same normalization scale |
| `velocity_magnitude` | `‖(x_t, y_t, z_t) − (x_{t−1}, y_{t−1}, z_{t−1})‖ × motion_weight` | Frame-to-frame 3D motion speed, weighted by `--motion-weight` |

**Normalization invariants:**
- Translation: All coordinates relative to wrist (landmark 0)
- Scale: Normalized by max distance from wrist to any other landmark
- Position-only by default: `motion_weight = 0.0` (velocity disabled)

### Hand Tracking

- **Upstream Handedness Ordering**: `handpose_det` assigns `is_left_hand`; `hands[0]` is left and `hands[1]` is right
- **No Local Left/Right Reassignment**: Sign recognition does not sort hands by x-coordinate or reclassify handedness
- **Occlusion Handling**: Zero-padding for missing hand slots (`present = false`)
- **Confidence Filtering**: Hands below `min_confidence` are zero-padded

### BiLSTM Encoder

- **Input**: `[1, 30, 168]` — batch × sequence_length × feature_dim
- **Output**: `[1, 30, 128]` — batch × sequence_length × frame_embedding_dim
- **Runtime**: RKNN NPU (single forward pass, ~10-20ms)
- **Architecture**: Bidirectional LSTM with 128 hidden units

### DTW Matching

1. **Distance Metric**: Normalized Euclidean distance per frame pair: `√(∑(q_i − s_j)² / embedding_dim)`
2. **Sakoe-Chiba Window**: `max(|Q|−|S|, round(max_len × window_ratio), 1)` — constrains alignment path to prevent pathological warping
3. **Path Normalization**: Accumulated DTW cost ÷ number of alignment steps (prevents length bias)
4. **Confidence**: `1 / (1 + normalized_dtw_distance)` — distance-to-confidence conversion
5. **Multi-Sample Matching**: Each gesture can have multiple prototype samples; the matcher scores a gesture by its **best sample** (minimum DTW distance)

**Vocabulary boundary:** SQLite database `prototypes.sqlite` stores all gesture labels and their encoded prototype samples. Adding/removing signs updates only this database, not the BiLSTM encoder model.

### Result Filtering

1. **Confidence Threshold**: Reject if `top1_confidence < confidence_threshold` (default 0.6)
2. **Margin Filter**: Reject if `top1_confidence − top2_confidence < confidence_margin` (default 0.1)
3. **Duplicate Suppression**: Do not publish the same recognized gesture again within `duplicate_suppression_ms` (default 1000 ms)

**Rationale:** Prevents ambiguous gestures where multiple classes have similar scores. Both thresholds must pass for a valid recognition.

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
    └─► 168-dim Feature Vector
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
install/bin/signlang_det \
    --input-service handpose_result \
    --output-service signlang_result
```

### Custom Recognition Parameters

```bash
# Longer window, stricter confidence
install/bin/signlang_det \
    --input-service handpose_result \
    --output-service signlang_result \
    --sequence-length 45 \
    --overlap-ratio 0.3 \
    --confidence-threshold 0.7 \
    --confidence-margin 0.15 \
    --duplicate-suppression-ms 1500
```

### With Motion Features

```bash
# Include velocity in recognition (0.3 = 30% motion weight)
install/bin/signlang_det \
    --input-service handpose_result \
    --output-service signlang_result \
    --motion-weight 0.3 \
    --dtw-window-ratio 0.6
```

### Specific NPU Core

```bash
install/bin/signlang_det \
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
| `feature_extractor.{cpp,hpp}` | Hand tracking and 168-dim feature extraction |
| `keypoint_ring_buffer.{cpp,hpp}` | Thread-safe circular buffer for sliding windows |
| `iceoryx_gateway.{cpp,hpp}` | iceoryx2 subscriber and publishers |
| `signlang_result.{cpp,hpp}` | IPC message definitions |

## IPC Data Structures

### SignlangResult (Published)

```cpp
struct GestureCandidate {
    std::uint32_t gesture_id;
    float confidence;
    float distance;
    std::array<char, kMaxGestureNameLength> gesture_name;
};

struct SignlangResult {
    std::uint64_t sequence_number;
    std::uint64_t timestamp_ns;
    std::uint64_t window_start_sequence;
    std::uint64_t window_end_sequence;
    std::uint32_t sequence_length;
    float overlap_ratio;
    float inference_time_ms;
    bool recognized;
    std::uint32_t gesture_id;
    float confidence;
    float second_confidence;
    float confidence_margin;
    float distance;
    std::array<char, kMaxGestureNameLength> gesture_name;
    std::uint32_t candidate_count;
    std::array<GestureCandidate, kMaxGestureCandidates> candidates;
};
```

### Prototype Database Format

`prototypes.sqlite` is the runtime vocabulary boundary. It stores gesture labels and encoded prototype samples in a single file, so adding or removing signs updates only this database, not the BiLSTM encoder model.

**Required schema:**

```sql
CREATE TABLE meta (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
);

CREATE TABLE gestures (
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL,
  enabled INTEGER NOT NULL DEFAULT 1
);

CREATE TABLE samples (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  gesture_id INTEGER NOT NULL,
  frame_count INTEGER NOT NULL,
  embedding_dim INTEGER NOT NULL,
  dtype TEXT NOT NULL DEFAULT 'f32',
  data BLOB NOT NULL,
  weight REAL NOT NULL DEFAULT 1.0,
  FOREIGN KEY (gesture_id) REFERENCES gestures(id)
);

CREATE INDEX idx_samples_gesture_id ON samples(gesture_id);
```

**Required metadata:**

```sql
INSERT INTO meta(key, value) VALUES
  ('schema_version', '1'),
  ('embedding_dim', '128');
```

**Sample storage format:**
- Each sample `data` BLOB stores contiguous `float32` values with shape `[frame_count, embedding_dim]`
- Row-major layout: `[frame0_emb0, frame0_emb1, ..., frame0_emb127, frame1_emb0, ...]`
- The loader reads all enabled gestures and samples into memory at startup
- DTW matching does not query SQLite during recognition (all-in-memory operation)

## Performance Characteristics

- **BiLSTM inference time**: ~10-20ms for 30-frame window on single NPU core (RK3588)
- **DTW matching time**: ~5-15ms per prototype (CPU, depends on prototype count and sequence length)
- **Total latency**: ~50-100ms per recognition (30-frame window @ 30 fps = 1s)
- **Memory**: ~12MB model footprint + SQLite prototype database (~1-5MB depending on vocabulary size)
- **CPU usage**: <5% on single core (event-driven, no polling; Cortex-A76 @ 2.4GHz)
- **Recognition rate**: ~10-12 recognitions per second with 20% overlap (0.8s hop)

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

- Check if the gesture is enabled in `conf/prototypes.sqlite`
- Verify at least one sample exists for that gesture
- Try longer sequence length: `--sequence-length 45`

### RKNN Initialization Failed

Ensure RKNN model is present and `librknnrt.so` is in library path:
```bash
ls models/bilstm/
export LD_LIBRARY_PATH=/path/to/lib:$LD_LIBRARY_PATH
```

### Module Not Processing Frames

Check if state machine is in the correct state:
- Sign language recognition only runs in SignLanguageChat or SignLanguageAi states
- Check state machine logs or trigger state change
