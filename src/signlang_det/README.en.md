# Sign Language Detection

[简体中文](README.md) | [English](README.en.md)

Core sign language recognition module. Extracts temporal features from hand keypoints, performs BiLSTM encoding (RKNN NPU) with DTW prototype matching for sign vocabulary recognition, and provides gesture library management.

## Features

- Subscribes to hand keypoint detections from iceoryx2
- Feature extraction: normalizes keypoint coordinates, computes relative hand positions and motion features, producing 168-dim (single hand) or 336-dim (dual hand) feature vectors
- BiLSTM encoding: runs a bidirectional LSTM encoder on the RKNN NPU to map temporal feature sequences into embedding vectors
- DTW matching: searches the prototype database for the best-matching gesture and produces recognition results
- Prototype database management: SQLite-backed persistent storage with runtime CRUD operations
- Exposes prototype reload and gesture management interfaces via iceoryx2 request-response
- Duplicate suppression and consecutive-hit filtering to reduce false positives

## Processing Pipeline

```text
HandPoseDetection[] → FeatureExtractor (normalize / 168-dim) → KeypointRingBuffer (temporal window)
                                                                      ↓
                                                               BiLSTM Encoder (RKNN / NPU)
                                                                      ↓
                                                               DTW Matcher → PrototypeStore (SQLite)
                                                                      ↓
                                                               SignlangResult → iceoryx2
```

## CLI Arguments

| Argument | Description |
| --- | --- |
| `-i, --input-service <name>` | iceoryx2 handpose input service (required) |
| `-o, --output-service <name>` | iceoryx2 recognition result output service (required) |
| `--prototype-control-service <name>` | Prototype control service name |
| `--gesture-management-service <name>` | Gesture management service name |
| `-m, --model <path>` | BiLSTM encoder RKNN model path |
| `--prototypes <path>` | Gesture prototype SQLite database path |
| `--sequence-length <frames>` | Sliding window frame count (default: 30) |
| `--overlap-ratio <0.0-0.9>` | Window overlap ratio (default: 0.2) |
| `--min-confidence <0.0-1.0>` | Minimum keypoint confidence (default: 0.3) |
| `--motion-weight <0.0-1.0>` | Motion feature weight (default: 0.0) |
| `--dtw-window-ratio <0.0-1.0>` | DTW window constraint ratio (default: 0.5) |
| `--confidence-threshold <0.0-1.0>` | Recognition threshold (default: 0.6) |
| `--confidence-margin <0.0-1.0>` | Top-1 to top-2 confidence margin (default: 0.1) |
| `--duplicate-suppression-ms <ms>` | Duplicate result suppression window (default: 1000) |
| `--upload-window-overlap <ratio>` | Upload sequence split overlap (default: 0.5) |
| `--max-representative-samples <n>` | Max prototype samples per gesture (default: 3) |
| `--consecutive-hit-windows <n>` | Required consecutive hit windows (default: 2) |
| `--subscriber-buffer <n>` | iceoryx2 subscriber queue size |
| `--npu-core <mask>` | NPU core mask |

## IPC Interfaces

| Interface | Pattern | Description |
| --- | --- | --- |
| Recognition Result Output | Publish-Subscribe | Publishes `SignlangResult` to `dataflow_dispatcher` |
| Prototype Control | Request-Response | Supports `ReloadPrototypes` and `GetStatus` commands |
| Gesture Management | Request-Response | Supports gesture CRUD (8 command types) |

## Gesture Management

The `GestureManagementService` handles requests originating from `signlang_manager` (BLE entry point):

- `ListGestures` — List all registered gestures
- `AddGestureBegin/Chunk/Commit/Abort` — Chunked upload of new gesture samples
- `DeleteGestureById/DeleteGestureByName` — Delete a specific gesture
- `GetStatus` — Query current vocabulary status

Newly uploaded samples are automatically encoded and persisted to the SQLite prototype database.

## Dependencies

- RKNN API (BiLSTM encoder inference)
- SQLite / SQLiteCpp (prototype database persistence)
- iceoryx2 (publish-subscribe, request-response)
