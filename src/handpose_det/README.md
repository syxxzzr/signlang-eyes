# handpose_det — Hand Pose Detection (YOLOv8)

## Overview

The **handpose_det** module performs real-time hand detection and 21-keypoint localization using a YOLOv8 nano model running on the RKNN NPU. It subscribes to video frames, runs inference with letterbox preprocessing and NMS post-processing, and publishes hand detection results. Supports dual-hand detection with configurable confidence and IoU thresholds.

- **Executable**: `handpose_det` (installed under `bin/`)
- **IPC Pattern**: Publish-Subscribe (subscriber + publisher) + Event/Blackboard (state control)
- **Input**: RGB24 video byte slice with `signlang::video_frontend::VideoFrameMetadata` user header from iceoryx2
- **Output**: `iox2::bb::Slice<signlang::handpose_det::HandPoseDetection>` with `HandPoseFrameMetadata` user header on iceoryx2
- **Model**: YOLOv8 nano hand-pose (anchor-free, RKNN-accelerated)

## Command-Line Parameters

### IPC (Required)

| Parameter | Description |
|-----------|-------------|
| `--input-service` / `-i` | iceoryx2 video input publish-subscribe service name |
| `--output-service` / `-o` | iceoryx2 handpose detection result output service name |

### State Gate (Optional)

| Parameter | Description |
|-----------|-------------|
| `--state-event-service` | iceoryx2 event service for global app state change notifications |
| `--state-blackboard-service` | iceoryx2 blackboard service for global app state storage |

When both state gate services are provided, hand pose detection reads the current blackboard state at startup and follows subsequent app state events. Without state gate services, it stays enabled.

### Model Paths

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--model` / `-m` | `models/yolov8n-handpose/yolov8_handpose.rknn` | YOLOv8 hand-pose RKNN model path |

### Detection

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--confidence` | `0.5` | Minimum detection confidence (0.0-1.0) |
| `--nms` | `0.4` | NMS IoU threshold for suppressing overlapping detections (0.0-1.0) |
| `--keypoints` | `21` | Expected keypoint count per detection (must be > 0) |
| `--max-detections` | `16` | Maximum number of detections published per frame (must be > 0) |

### Performance

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--npu-core` | `auto` | NPU core mask: `auto`, `0`, `1`, `2`, `0_1`, `0_1_2`, `all` |
| `--subscriber-buffer` | `2` | iceoryx2 subscriber queue size (must be > 0) |
| `--verbose` | off | Print model tensor input/output details on startup |

## Technical Details

### YOLOv8 Output Format

Per candidate, the model outputs 68 channels:
- `[0:4]`: Bounding box (center_x, center_y, width, height)
- `[4]`: Object confidence score
- `[5:68]`: 21 keypoints × 3 values (x, y, confidence) = 63 channels

The output tensor supports:
- **Data types**: Float32, Float16, Int8, UInt8 (auto-detected via `rknn_tensor_attr.type`)
- **Memory**: Pre-allocated tensor memory (`rknn_tensor_mem`) for zero-copy I/O on NPU

### Preprocessing

- **Letterbox**: Aspect-ratio-preserving scale-to-fit with black padding
- **Format**: RGB24 → NHWC uint8 input to the model
- **No color space conversion**: Input video is already RGB24

### Post-processing Pipeline

1. Parse raw output tensor values per candidate (confidence + box + keypoints)
2. Filter by `--confidence` threshold
3. Unproject coordinates from letterbox space to original image space
4. Sort candidates by confidence (descending)
5. Apply NMS with `--nms` IoU threshold
6. Output up to `--max-detections` results

### Event-Driven Subscription

The module uses iceoryx2 `Node::wait()` for event-driven video frame reception:
- No periodic polling when idle
- Zero CPU usage while waiting for frames
- 5ms timeout for responsive shutdown
- Replaces legacy polling loops for better power efficiency

### State Control

When state gate services are provided:
- Reads current state from blackboard at startup
- Subscribes to state change events
- Only processes frames when in sign language states (SignLanguageChat, SignLanguageAi)
- Skips inference when disabled to save power

### 21 Hand Keypoints

Uses the standard MediaPipe-compatible hand landmark layout:
- **0**: Wrist
- **1–4**: Thumb (CMC → MCP → IP → TIP)
- **5–8**: Index finger (MCP → PIP → DIP → TIP)
- **9–12**: Middle finger (MCP → PIP → DIP → TIP)
- **13–16**: Ring finger (MCP → PIP → DIP → TIP)
- **17–20**: Pinky (MCP → PIP → DIP → TIP)

## Architecture

```
Video Subscriber (iceoryx2)
    │ [event-driven via Node::wait()]
    ▼
State Gate (optional)
    │
    ▼ [enabled]
HandposePreprocessor
    │
    ├─► Letterbox Scaling
    │   (preserve aspect ratio)
    │       │
    │       ▼
    │   RGB24 NHWC uint8
    │       │
    └─► HandposeModel (RKNN NPU)
            │
            ├─► Inference (YOLOv8n)
            │       │
            │       ▼
            │   Raw Detections [N×68]
            │       │
            │       ▼
            │   Confidence Filter
            │       │
            │       ▼
            │   Coordinate Unprojection
            │       │
            │       ▼
            │   NMS (IoU threshold)
            │       │
            └─► Top-K Selection
                    │
                    ▼
            HandPoseTransport
             (iceoryx2 pub)
```

## Dependencies

- **RKNN Runtime**: NPU inference (`librknnrt.so`)
- **iceoryx2**: Zero-copy IPC
- **spdlog**: Logging

## Usage Examples

### Basic Usage

```bash
./handpose_det \
    --input-service video_capture \
    --output-service handpose_result
```

### With State Control

```bash
./handpose_det \
    --input-service video_capture \
    --output-service handpose_result \
    --state-event-service app_state_event \
    --state-blackboard-service app_state_blackboard
```

### Custom Detection Parameters

```bash
# Higher confidence, track up to 2 hands
./handpose_det \
    --input-service video_capture \
    --output-service handpose_result \
    --confidence 0.6 \
    --nms 0.3 \
    --max-detections 2
```

### Specific NPU Core

```bash
./handpose_det \
    --input-service video_capture \
    --output-service handpose_result \
    --npu-core 2
```

### Debug Mode

```bash
# Print tensor details on startup
./handpose_det \
    --input-service video_capture \
    --output-service handpose_result \
    --verbose
```

## File Organization

| File | Description |
|------|-------------|
| `main.cpp` | Entry point; signal handling, main inference loop |
| `program_options.{cpp,hpp}` | CLI argument parsing via cxxopts |
| `handpose_model.{cpp,hpp}` | YOLOv8 RKNN model wrapper with NMS |
| `handpose_preprocessor.{cpp,hpp}` | Letterbox scaling and format conversion |
| `handpose_transport.{cpp,hpp}` | iceoryx2 subscriber, publisher, state control |
| `handpose_frame.hpp` | IPC message definitions |

## IPC Data Structures

### HandPoseFrameMetadata (User Header)

```cpp
struct HandPoseFrameMetadata {
    std::uint64_t timestamp_ns;
    std::uint64_t sequence_number;
    std::uint32_t detection_count;
    float inference_time_ms;
};
```

### HandPoseDetection (Payload)

```cpp
struct HandPoseDetection {
    HandPoseBox box;
    std::array<HandPoseKeypoint, 21> keypoints;
};

struct HandPoseBox {
    float center_x;
    float center_y;
    float width;
    float height;
    float confidence;
};

struct HandPoseKeypoint {
    float x;
    float y;
    float confidence;
};
```

## Performance Characteristics

- **Inference time**: ~15-25ms per frame at 640×480 on single NPU core (RK3588)
- **Throughput**: 30-60 fps sustained
- **Memory**: ~4MB model footprint
- **CPU usage**: <3% on single core (event-driven, no polling)
- **Latency**: ~30-50ms glass-to-glass (camera → detection → publish)
- **Accuracy**: ~95% AP@0.5 on MediaPipe hand dataset

## Detection Threshold Guidelines

| Confidence | Behavior | Use Case |
|------------|----------|----------|
| 0.3-0.4 | Sensitive | Capture subtle gestures, more false positives |
| 0.5 (default) | Balanced | General purpose, good precision/recall |
| 0.6-0.7 | Conservative | Reduce false positives, may miss some gestures |
| 0.8+ | Very strict | Only very clear hand poses |

## Troubleshooting

### No Video Received

Check that video_frontend is running and publishing to the correct service:
```bash
# Should show video_capture service
iox2-list
```

### Low Detection Rate

Lower the confidence threshold:
```bash
./handpose_det --confidence 0.3
```

### Too Many False Positives

Increase confidence and/or tighten NMS:
```bash
./handpose_det --confidence 0.6 --nms 0.3
```

### RKNN Initialization Failed

Ensure RKNN model is present and `librknnrt.so` is in library path:
```bash
ls models/yolov8n-handpose/
export LD_LIBRARY_PATH=/path/to/lib:$LD_LIBRARY_PATH
```

### Module Not Processing Frames

Check if state machine is in the correct state:
- Hand pose detection only runs in SignLanguageChat or SignLanguageAi states
- Check state machine logs or trigger state change
