# handpose_det — Hand Pose Detection (YOLOv8)

## Overview

The **handpose_det** module performs real-time hand detection and 21-keypoint localization using a YOLOv8 nano model running on the RKNN NPU. It subscribes to video frames, runs inference with letterbox preprocessing and NMS post-processing, and publishes hand detection results. Supports dual-hand detection with configurable confidence and IoU thresholds.

- **Executable**: `handpose_det` (installed under `bin/`)
- **IPC Pattern**: Publish-Subscribe (subscriber + publisher) + Event/Blackboard (state control)
- **Input**: video byte slice with `signlang::video_frontend::VideoFrameMetadata` user header from iceoryx2
- **Output**: `iox2::bb::Slice<signlang::handpose_det::HandPoseDetection>` with `HandPoseFrameMetadata` user header on iceoryx2
- **Model**: YOLOv8 nano hand-pose (anchor-free, RKNN-accelerated)

## File Inventory

| File | Description |
|------|-------------|
| `main.cpp` | Entry point; signal handling, main inference loop |
| `program_options.{cpp,hpp}` | CLI argument parsing via cxxopts |
| `handpose_model.{cpp,hpp}` | YOLOv8 RKNN model: load, configure I/O via tensor memory, run inference, parse output with NMS |
| `handpose_preprocessor.{cpp,hpp}` | Letterbox scaling: aspect-ratio-preserving resize with padding |
| `handpose_transport.{cpp,hpp}` | iceoryx2 subscriber (video), publisher (hand pose), event/blackboard (state control) |
| `rknn_runtime.{cpp,hpp}` | Dynamic RKNN runtime library loader (`dlopen` wrapper for `librknnrt.so`) |
| `handpose_frame.hpp` | `HandPoseDetection`, `HandPoseBox`, `HandPoseKeypoint` IPC message definitions (shared header) |

## Command-Line Parameters

### IPC (Required)

| Parameter | Description |
|-----------|-------------|
| `--input-service` / `-i` | iceoryx2 video input publish-subscribe service name |
| `--output-service` / `-o` | iceoryx2 handpose detection result output service name |
| `--state-event-service` | iceoryx2 event service for global app state change notifications |
| `--state-blackboard-service` | iceoryx2 blackboard service for global app state storage |

### Model Paths

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--model` / `-m` | `models/yolov8n-handpose/yolov8_handpose.rknn` | YOLOv8 hand-pose RKNN model path |
| `--rknn-runtime` | *(empty = system loader)* | Path to `librknnrt.so`; empty uses default system library search |

### Detection

| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| `--confidence` | `0.5` | `(0.0, 1.0)` | Minimum detection confidence; candidates below this are discarded |
| `--nms` | `0.4` | `(0.0, 1.0)` | NMS IoU threshold for suppressing overlapping detections |
| `--keypoints` | `21` | `1–21` | Expected keypoint count per detection |
| `--max-detections` | `16` | `1–16` | Maximum number of detections published per frame |

### Performance

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--npu-core` | `auto` | NPU core mask: `auto`, `0`, `1`, `2`, `0_1`, `0_1_2`, `all` |
| `--subscriber-buffer` | `1` | `1–8` | iceoryx2 subscriber queue size |
| `--verbose` | off | Print model tensor input/output details on startup |

## Technical Details

### YOLOv8 Output Format

Per candidate, the model outputs 68 channels:
- `[0:4]`: Bounding box (center_x, center_y, width, height)
- `[4]`: Object confidence score
- `[5:]`: 21 keypoints × 3 values (x, y, confidence) = 63 channels

The output tensor supports:
- **Data types**: Float32, Float16, Int8, UInt8 (auto-detected via `rknn_tensor_attr.type`)
- **Memory**: Pre-allocated tensor memory (`rknn_tensor_mem`) for zero-copy I/O on NPU

### Preprocessing

- **Letterbox**: Aspect-ratio-preserving scale-to-fit with black padding
- **Format**: RGB → NHWC uint8 input to the model

### Post-processing Pipeline

1. Parse raw output tensor values per candidate (confidence + box + keypoints)
2. Filter by `--confidence` threshold
3. Unproject coordinates from letterbox space to original image space
4. Sort candidates by confidence (descending)
5. Apply NMS with `--nms` IoU threshold
6. Output up to `--max-detections` results

### 21 Hand Keypoints

Uses the standard MediaPipe-compatible hand landmark layout:
- **0**: Wrist
- **1–4**: Thumb (CMC → MCP → IP → TIP)
- **5–8**: Index finger (MCP → PIP → DIP → TIP)
- **9–12**: Middle finger (MCP → PIP → DIP → TIP)
- **13–16**: Ring finger (MCP → PIP → DIP → TIP)
- **17–20**: Pinky (MCP → PIP → DIP → TIP)

## Dependencies

- **RKNN Runtime**: NPU inference (`librknnrt.so`, dynamically loaded)
- **iceoryx2**: Zero-copy IPC
- **pthread**: Thread synchronization

## Usage Example

```bash
./handpose_det \
    --input-service video_capture \
    --output-service handpose_result \
    --state-event-service app_state_event \
    --state-blackboard-service app_state_blackboard \
    --confidence 0.5 \
    --nms 0.4 \
    --max-detections 2 \
    --npu-core auto
```
