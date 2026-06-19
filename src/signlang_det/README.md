# signlang_det — Sign Language Recognition (BiLSTM + DTW)

## Overview

The **signlang_det** module performs real-time dual-hand sign language recognition using a hybrid BiLSTM + DTW (Dynamic Time Warping) architecture. It subscribes to hand pose detection results, extracts 126-dimensional spatial-temporal features, encodes them with a BiLSTM on the RKNN NPU, and matches the resulting embeddings against pre-recorded gesture prototypes using DTW for speed-invariant recognition.

- **Executable**: `signlang_det` (installed under `bin/`)
- **IPC Pattern**: Publish-Subscribe (subscriber + publisher) + Event/Blackboard (state control)
- **Input**: `iox2::bb::Slice<signlang::handpose_det::HandPoseDetection>` with `HandPoseFrameMetadata` user header from iceoryx2
- **Output**: `signlang::signlang_det::SignlangResult` on iceoryx2
- **Model**: BiLSTM encoder (RKNN NPU) + DTW matching (CPU)

## File Inventory

| File | Description |
|------|-------------|
| `main.cpp` | Entry point; dual-thread orchestration (receiver + inference thread) |
| `program_options.{cpp,hpp}` | CLI argument parsing via cxxopts |
| `signlang_model.{cpp,hpp}` | BiLSTM RKNN model + DTW matching: load, encode sequence, compute DTW distances, confidence filtering |
| `feature_extractor.{cpp,hpp}` | Hand tracking (slot-based L→R ordering), 126-dim feature extraction with coordinate normalization and velocity |
| `keypoint_ring_buffer.{cpp,hpp}` | Thread-safe circular buffer with condition variable for sliding window accumulation |
| `iceoryx_gateway.{cpp,hpp}` | iceoryx2 subscriber (handpose), publisher (signlang results), event/blackboard (state control) |
| `signlang_result.{cpp,hpp}` | `FeatureVector`, `KeypointFeature`, `HandFeatures`, `SignlangResult` data structure definitions |

## Command-Line Parameters

### IPC (Required)

| Parameter | Description |
|-----------|-------------|
| `--input-service` / `-i` | iceoryx2 handpose detection input service name |
| `--output-service` / `-o` | iceoryx2 sign language recognition result output service name |
| `--state-event-service` | iceoryx2 event service for global app state change notifications |
| `--state-blackboard-service` | iceoryx2 blackboard service for global app state storage |

### Model Paths

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--model` / `-m` | `models/signlang/signlang.rknn` | BiLSTM encoder RKNN model path |
| `--label-map` | `models/signlang/labels.txt` | Gesture label mapping file (one label per line, line number = gesture ID) |
| `--prototypes` | `models/signlang/prototypes.bin` | Encoded gesture prototype database (binary format) |

### Feature & Window

| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| `--sequence-length` | `30` | `1–120` | Sliding window size in frames (30 frames × 126 features) |
| `--overlap-ratio` | `0.2` | `[0.0, 1.0)` | Overlap ratio between adjacent sliding windows |
| `--min-confidence` | `0.3` | `[0.0, 1.0]` | Minimum hand keypoint detection confidence; low-confidence hands are zero-padded |

### Feature Weighting

| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| `--motion-weight` | `0.0` | `[0.0, 1.0]` | Velocity (motion) feature weight. `0.0` = position-only; `1.0` = full motion contribution |

### DTW Matching

| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| `--dtw-window-ratio` | `0.5` | `[0.0, 1.0]` | Sakoe-Chiba DTW window ratio. Larger = more tolerant of speed variation; `1.0` = unconstrained |
| `--confidence-threshold` | `0.6` | `[0.0, 1.0]` | Minimum recognition confidence; results below this are discarded |
| `--confidence-margin` | `0.1` | `[0.0, 1.0]` | Minimum gap between top-1 and top-2 confidence; prevents ambiguous recognition |

### Performance

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--npu-core` | `auto` | NPU core mask: `auto`, `0`, `1`, `2`, `0_1`, `0_1_2`, `all` |
| `--subscriber-buffer` | `2` | `1–8` | iceoryx2 subscriber queue size |

## Technical Details

### Feature Extraction (126-Dimensional)

Each frame produces a 126-dim feature vector (2 hands × 21 keypoints × 3 channels):

| Channel | Formula | Description |
|---------|---------|-------------|
| `normalized_x` | `(kp.x − wrist.x) / scale` | X-coordinate relative to wrist, normalized by the maximum wrist-relative keypoint distance |
| `normalized_y` | `(kp.y − wrist.y) / scale` | Y-coordinate relative to wrist, normalized by the same scale as X |
| `velocity_magnitude` | `‖(x_t, y_t) − (x_{t−1}, y_{t−1})‖ × motion_weight` | Frame-to-frame motion speed, scaled by motion weight |

### Hand Tracking

- **Left-to-Right Ordering**: Hands sorted by x-coordinate per frame (`hands[0]` = left, `hands[1]` = right)
- **Slot-Based Tracking**: Hands matched to previous-frame slots by center-point distance
- **Occlusion Handling**: Zero-padding for missing hand slots (`present = false`)

### BiLSTM Encoder

- **Input**: `[1, 30, 126]` — batch × sequence_length × feature_dim
- **Output**: `[1, 30, 128]` — batch × sequence_length × frame_embedding_dim
- **Runtime**: RKNN NPU (single forward pass)

### DTW Matching

1. **Distance Metric**: Normalized Euclidean distance per frame pair: `√(∑(q_i − s_j)² / dim)`
2. **Sakoe-Chiba Window**: `max(|Q|−|S|, round(max_len × window_ratio), 1)`
3. **Path Normalization**: Accumulated DTW cost ÷ number of alignment steps
4. **Confidence**: `1 / (1 + normalized_dtw_distance)`

### Dual-Threshold Filtering

1. **Confidence Threshold**: Reject if `top1_confidence < confidence_threshold`
2. **Margin Filter**: Reject if `top1_confidence − top2_confidence < confidence_margin`

### Thread Architecture

- **Receiver Thread**: Subscribes to handpose → feature extraction → ring buffer push
- **Inference Thread**: Waits for full window → flatten features → BiLSTM encode → DTW match → filter → publish

## Usage Example

```bash
./signlang_det \
    --input-service handpose_result \
    --output-service signlang_result \
    --state-event-service app_state_event \
    --state-blackboard-service app_state_blackboard \
    --model models/signlang/signlang.rknn \
    --label-map models/signlang/labels.txt \
    --prototypes models/signlang/prototypes.bin \
    --sequence-length 30 \
    --overlap-ratio 0.2 \
    --min-confidence 0.3 \
    --motion-weight 0.0 \
    --dtw-window-ratio 0.5 \
    --confidence-threshold 0.6 \
    --confidence-margin 0.1
```
