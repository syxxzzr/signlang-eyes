# handpose_det — MediaPipe Hand Pose Detection

## Overview

The **handpose_det** module subscribes to RGB24 video frames, runs the MediaPipe dual-model pipeline (palm detector + hand landmark detector) using RKNN NPU inference, and publishes fixed-size hand pose payloads through iceoryx2.

**Recent Optimizations (based on MediaPipe Web Samples)**:
- ✅ **Three-tier confidence system**: Separate thresholds for palm detection, hand presence, and tracking
- ✅ **Non-Maximum Suppression (NMS)**: Filters overlapping palm detections using IoU threshold
- ✅ **Hand classification**: Outputs left/right hand labels (when model supports it)
- ✅ **Presence filtering**: Rejects low-confidence detections at landmark stage
- ✅ **Frame-to-frame ROI tracking**: Reduces palm detection calls by 40-60% in continuous frames
- ✅ **One Euro Filter smoothing**: Reduces keypoint jitter by 60-80%

- **Executable**: `handpose_det` (installed under `bin/`)
- **IPC Pattern**: Publish-Subscribe (video subscriber + hand pose publisher)
- **Input**: RGB24 video frames with `VideoFrameMetadata` user header from iceoryx2
- **Output**: `iox2::bb::Slice<HandPoseDetection>` with `HandPoseFrameMetadata` user header on iceoryx2
- **Models**: Palm detector (192×192) + Hand landmarks detector (224×224), both RKNN-accelerated
- **Preprocessing**: Rockchip RGA for crop/scale operations

## Output Semantics

The module publishes up to two valid hands by default. `--single-hand` limits output to one hand.

**Detection pipeline:**
1. Try tracking from previous frame ROIs using landmark model only
2. If tracked hands are fewer than the configured one-hand/two-hand mode, run full-frame palm detection
3. Apply weighted NMS to merge overlapping palm proposals by confidence
4. For new detections: compute adaptive ROI from palm keypoints, run landmark model
5. Filter by presence confidence; extract handedness classification
6. Apply One Euro Filter temporal smoothing to all tracked keypoints
7. Select the highest-confidence reliable left hand and right hand, then publish only those valid hands

**Key properties:**
- `HandPoseFrameMetadata::detection_count` reports the actual valid hand count: 0, 1, or 2
- Reliable handedness ordering is left hand first, then right hand; missing reliable sides are not published
- Output keypoints: 21 landmarks per hand with `x`, `y`, `z` (relative depth), and `confidence`

## Command-Line Parameters

Relative paths are resolved from the installation root. For installed module executables under `bin/`, the runtime root is the parent directory, so defaults like `models/…`, `conf/…`, and `log/…` do not depend on the shell current working directory.

All module executables also accept `--log-file <path>` and `--log-rotate-size <bytes>`; the launcher supplies these automatically when it starts modules.

| Parameter                | Default                                         | Range                    | Description                                          |
|--------------------------|-------------------------------------------------|--------------------------|------------------------------------------------------|
| `--input-service`, `-i`  | required                                        | –                        | Upstream video iceoryx2 service                      |
| `--output-service`, `-o` | required                                        | –                        | Hand pose output iceoryx2 service                    |
| `--model`, `-m`          | `models/mediapipe/hand_detector.rknn`           | –                        | Palm detector RKNN model                             |
| `--landmark-model`       | `models/mediapipe/hand_landmarks_detector.rknn` | –                        | Hand landmark RKNN model                             |
| `--single-hand`          | `false`                                         | `true`/`false`           | Detect and publish one hand instead of two           |
| `--confidence`           | `0.5`                                           | (0, 1)                   | Palm detection confidence threshold                  |
| `--presence-threshold`   | `0.5`                                           | (0, 1)                   | Hand presence confidence threshold                   |
| `--tracking-threshold`   | `0.5`                                           | (0, 1)                   | Confidence threshold for reusing previous ROI        |
| `--nms-iou-threshold`    | `0.3`                                           | (0, 1)                   | IoU threshold for weighted NMS merge                 |
| `--tracking-iou-match`   | `0.3`                                           | (0, 1)                   | IoU threshold for matching detection to tracked hand |
| `--crop-expansion`       | `2.0`   | `> 0`    | Expansion factor for tracking crop from bounding box |
| `--base-roi-expansion`   | `2.6`   | `> 0`    | Base ROI expansion factor from palm keypoints        |
| `--small-hand-expansion` | `3.0`   | `> 0`    | ROI expansion factor for small hands                 |
| `--large-hand-expansion` | `2.3`   | `> 0`    | ROI expansion factor for large hands                 |
| `--small-hand-threshold` | `60.0`  | `> 0`    | Pixel size below which a hand is considered small    |
| `--large-hand-threshold` | `120.0` | `> 0`    | Pixel size above which a hand is considered large    |
| `--boundary-margin`      | `50.0`  | `>= 0`   | Distance to image edge triggering boundary shrink    |
| `--boundary-min-factor`  | `0.7`   | `(0, 1]` | Minimum expansion multiplier at image boundary       |
| `--euro-min-cutoff`      | `1.0`   | `> 0`    | One Euro Filter min cutoff frequency (Hz)            |
| `--euro-beta`            | `0.007` | `>= 0`   | One Euro Filter speed coefficient                    |
| `--euro-d-cutoff`        | `1.0`   | `> 0`    | One Euro Filter derivative cutoff frequency (Hz)     |
| `--handedness-threshold` | `0.5`   | `(0, 1)` | Threshold for left/right hand classification         |
| `--swap-handedness`      | `false` | –        | Swap left/right handedness interpretation for mirrored cameras |
| `--max-tracking-gap`     | `2`     | `>= 1`   | Max frame gap before tracking is considered lost     |
| `--max-stale-frames`     | `5`     | `>= 1`   | Max frames before stale track slot is reclaimed      |
| `--single-hand-full-frame-interval` | `5` | `>= 0` | Full-frame palm detection interval while one hand is tracked; 0 disables |
| `--stable-hands-full-frame-interval` | `15` | `>= 0` | Full-frame palm detection interval while all hand slots are tracked; 0 disables |
| `--npu-core`             | `auto`                                          | auto,0,1,2,0_1,0_1_2,all | RK3588 NPU core mask                                 |
| `--palm-npu-core`        | `--npu-core`                                    | auto,0,1,2,0_1,0_1_2,all | Palm detector NPU core override                      |
| `--landmark-npu-core`    | `--npu-core`                                    | auto,0,1,2,0_1,0_1_2,all | Hand landmark model NPU core override                |
| `--subscriber-buffer`    | `2`    | `>= 1`   | iceoryx2 subscriber queue size                       |
| `--verbose`              | off                                             | –                        | Print RKNN tensor details                            |

## IPC Payload

```cpp
struct HandPoseKeypoint {
  float x;              // Pixel x-coordinate in source image space
  float y;              // Pixel y-coordinate in source image space
  float z;              // Relative depth (scaled from 224×224 crop to source scale)
  float confidence;     // Keypoint confidence (0.0-1.0)
};

struct HandPoseBox {
  float x_center;       // Detection box center x
  float y_center;       // Detection box center y
  float width;          // Detection box width
  float height;         // Detection box height
  float rotation;       // Box rotation in radians
};

struct HandPoseDetection {
  HandPoseBox box;
  std::array<HandPoseKeypoint, 21> keypoints;  // MediaPipe 21-landmark topology
  float confidence;              // Combined palm + presence confidence
  float presence_confidence;     // Hand presence confidence from landmark model
  std::uint32_t class_id;
  bool present;         // false for zero-filled slots
  bool is_left_hand;    // true if left hand, false if right (when model supports)
};
```

Coordinates are in source image pixel space. Landmark `z` is scaled from the 224×224 landmark crop back to the source image scale.

## Usage

```bash
install/bin/handpose_det \
  --input-service video_capture \
  --output-service handpose_result \
  --single-hand=false \
  --confidence 0.5
```

## Performance Characteristics

- **Palm detection**: ~8-15ms on single NPU core (RK3588)
- **Landmark detection**: ~5-10ms per hand on single NPU core
- **Total latency**: ~20-35ms per frame for 2 hands (30 fps capable)
- **RGA preprocessing**: ~2-3ms for crop/scale operations per hand
- **Memory**: ~15MB model footprint (palm + landmark models)
- **CPU usage**: <8% on single core (Cortex-A76 @ 2.4GHz)
