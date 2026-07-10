# Hand Pose Detection

[简体中文](README.md) | [English](README.en.md)

Hand keypoint detection and tracking module. Based on the MediaPipe architecture, runs palm detection and hand landmark (21 keypoints) models on the RK3588 NPU. Supports dual-hand tracking, temporal smoothing, and handedness classification.

## Features

- Subscribes to video frames from iceoryx2 (RGB24)
- Two-stage inference: palm detector locates hand regions → landmark model extracts 21 keypoints
- Supports dual-hand detection (configurable to single-hand via `--single-hand`)
- IoU-based tracking: prioritizes fast ROI-based tracking from the previous frame, with periodic full-frame detection
- One Euro Filter for keypoint temporal smoothing
- Left/right hand classification
- Publishes detection results over iceoryx2 publish-subscribe

## Processing Pipeline

```text
VideoFrame → PalmDetector (RKNN) → NMS → LandmarkDetector (RKNN)
                                              ↓
                                        Tracking & Smoothing (One Euro Filter)
                                              ↓
                                        HandPoseDetection[] → iceoryx2
```

## CLI Arguments

| Argument | Description |
| --- | --- |
| `-i, --input-service <name>` | iceoryx2 video input service (required) |
| `-o, --output-service <name>` | iceoryx2 handpose output service (required) |
| `-m, --model <path>` | Palm detector RKNN model path |
| `--landmark-model <path>` | Landmark detector RKNN model path |
| `--confidence <float>` | Palm detection confidence threshold (default: 0.5) |
| `--presence-threshold <float>` | Hand presence confidence (default: 0.5) |
| `--tracking-threshold <float>` | Tracking confidence threshold (default: 0.5) |
| `--nms-iou-threshold <float>` | NMS IoU threshold (default: 0.3) |
| `--tracking-iou-match <float>` | Tracking match IoU threshold (default: 0.3) |
| `--crop-expansion <float>` | Crop expansion factor (default: 2.0) |
| `--base-roi-expansion <float>` | Base ROI expansion factor (default: 2.6) |
| `--small-hand-expansion <float>` | Small hand additional expansion (default: 2.0) |
| `--large-hand-expansion <float>` | Large hand additional expansion (default: 0.0) |
| `--small-hand-threshold <float>` | Small hand size threshold (default: 0.05) |
| `--large-hand-threshold <float>` | Large hand size threshold (default: 0.15) |
| `--euro-min-cutoff <Hz>` | One Euro Filter minimum cutoff frequency |
| `--euro-beta <float>` | One Euro Filter speed coefficient |
| `--euro-d-cutoff <Hz>` | One Euro Filter derivative cutoff frequency |
| `--handedness-threshold <float>` | Handedness threshold (default: 0.5) |
| `--swap-handedness` | Swap left/right (for mirrored cameras) |
| `--max-tracking-gap <frames>` | Maximum tracking gap in frames |
| `--max-stale-frames <frames>` | Maximum stale frames before removal |
| `--full-frame-interval <frames>` | Full-frame detection interval (0 to disable) |
| `--single-hand` | Recognize only one hand |
| `--npu-core <mask>` | NPU core mask (palm model) |
| `--palm-npu-core <mask>` | Palm model NPU core (overrides --npu-core) |
| `--landmark-npu-core <mask>` | Landmark model NPU core |
| `--subscriber-buffer <n>` | iceoryx2 subscriber queue size |

## Design Notes

- Full-frame palm detection runs every 10 frames by default; remaining frames use ROI-based tracking for low latency
- NMS (Non-Maximum Suppression) eliminates overlapping detection boxes
- One Euro Filter provides adaptive smoothing: strong smoothing at low speeds, fast following at high speeds
- Palm-keypoint-based hand region cropping balances size adaptation with boundary protection
- DMA buffer management ensures efficient NPU data access on the RK3588

## Dependencies

- RKNN API (inference)
- RGA (DMA buffer management)
- iceoryx2 (publish-subscribe)
