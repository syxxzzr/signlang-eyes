# video_frontend — Video Capture & Publishing

## Overview

The **video_frontend** module captures video frames from a V4L2 (Video4Linux2) camera device and publishes RGB24 frames as an iceoryx2 byte slice with `VideoFrameMetadata` user-header metadata. It supports YUYV and MJPEG capture formats, normalizes both to RGB24, and applies hardware-accelerated format conversion and scaling via Rockchip RGA.

- **Executable**: `video_frontend` (installed under `bin/`)
- **IPC Pattern**: Publish-Subscribe (producer with user header)
- **Input**: V4L2 camera device (YUYV 4:2:2 or MJPEG)
- **Output**: `iox2::bb::Slice<std::uint8_t>` + `signlang::video_frontend::VideoFrameMetadata` user header on iceoryx2

## Command-Line Parameters

Relative paths are resolved from the installation root. For installed module executables under `bin/`, the runtime root is the parent directory, so defaults like `models/…`, `conf/…`, and `log/…` do not depend on the shell current working directory.

All module executables also accept `--log-file <path>` and `--log-rotate-size <bytes>`; the launcher supplies these automatically when it starts modules.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--device` / `-d` | *(required)* | V4L2 camera device name (e.g., `/dev/video0`) |
| `--service` / `-s` | *(required)* | iceoryx2 publish-subscribe service name for video output |
| `--capture-width` | (device default) | Requested camera capture width in pixels |
| `--capture-height` | (device default) | Requested camera capture height in pixels |
| `--output-width` | (matches capture) | Published output width in pixels |
| `--output-height` | (matches capture) | Published output height in pixels |
| `--fps` | `30` | Requested camera frame rate |
| `--mirror-output` | `false` | Horizontally mirror the published RGB output frame |
| `--cpu-core` | *(system default)* | Best-effort bind to one CPU core; invalid values or binding failures log a warning and keep system default scheduling |
| `--help` / `-h` | — | Print usage |

> **Note**: `--capture-width` and `--capture-height` must be specified together (or omitted together). Same rule applies to `--output-width` and `--output-height`.

## Technical Details

### Video Format

- **Capture Pixel Format**: YUYV 4:2:2 or MJPEG
- **Published Pixel Format**: RGB24
- **Output Frame Size**: `width × height × 3` bytes
- **Hardware Acceleration**: Rockchip RGA used for YUYV→RGB24 conversion, scaling, and optional horizontal mirroring

### Processing Pipeline

1. **YUYV capture**: RGA hardware converts YUYV to RGB24, scales, and optionally mirrors in a single operation
2. **MJPEG capture**: libjpeg-turbo decodes to RGB24, then RGA scales and optionally mirrors

### RGA Hardware Acceleration

The Rockchip RGA (Raster Graphic Acceleration) unit provides:
- Zero-copy format conversion (YUYV → RGB24) via DMA buffers
- Hardware scaling with bilinear interpolation
- ~50× performance improvement over CPU-based conversion
- Typical processing time: 2-5ms per frame at 1080p
- Eliminates CPU overhead for pixel format transformation

**RGA vs CPU Performance:**

| Operation | CPU (software) | RGA (hardware) | Speedup |
|-----------|----------------|----------------|---------|
| 1920×1080 YUYV→RGB24 | ~100ms | ~2ms | 50× |
| 640×480 YUYV→RGB24 + scale from 1080p | ~120ms | ~3ms | 40× |
| CPU utilization | 100% (1 core) | <5% | 20× |

### Published Sample Structure

Each published sample contains:
- **User header**: Width, height, pixel format, frame size in bytes
- **Timestamp**: Captured from `std::chrono::steady_clock`
- **Sequence number**: Monotonically increasing frame counter
- **Payload**: Raw RGB24 video bytes as a mutable byte slice

### Capture Loop

1. Dequeue buffer from V4L2
2. Convert and scale with RGA (YUYV) or decode + scale (MJPEG)
3. Publish via iceoryx2
4. Requeue buffer to V4L2 driver

## Architecture

```
V4L2 Device (/dev/video21)
        │
        ▼
V4l2CaptureDevice
(YUYV/MJPEG capture)
        │
        ▼
VideoProcessor
        │
        ├───────────────────┐
        ▼                   ▼
    YUYV Path          MJPEG Path
        │                   │
        ▼                   ▼
    RGA Convert      TurboJPEG Decode
  YUYV→RGB24+Scale      (RGB24)
        │                   │
        │                   ▼
        │              RGA Scale
        │              (if needed)
        │                   │
        └───────┬───────────┘
                ▼
         VideoPublisher
        (iceoryx2 pub)
```

## Dependencies

- **V4L2** (Linux kernel API): Camera capture
- **librga**: Rockchip RGA hardware acceleration
- **libjpeg-turbo**: MJPEG decode
- **iceoryx2**: Zero-copy IPC publishing
- **pthread**: Thread synchronization

## Usage Examples

### Basic Usage

```bash
# Capture from /dev/video0 at default resolution and 30 fps
install/bin/video_frontend \
    --device /dev/video0 \
    --service video_capture
```

### Custom Resolution

```bash
# Capture 1080p, publish 640x480
install/bin/video_frontend \
    --device /dev/video0 \
    --service video_capture \
    --capture-width 1920 \
    --capture-height 1080 \
    --output-width 640 \
    --output-height 480 \
    --fps 30
```

### High Frame Rate

```bash
# 60 fps capture
install/bin/video_frontend \
    --device /dev/video0 \
    --service video_capture \
    --fps 60
```

### List Available Devices

```bash
# List V4L2 devices
v4l2-ctl --list-devices

# List supported formats for a device
v4l2-ctl -d /dev/video0 --list-formats-ext
```

## File Organization

| File | Description |
|------|-------------|
| `main.cpp` | Entry point; main capture loop |
| `program_options.{cpp,hpp}` | CLI argument parsing via cxxopts |
| `v4l2_capture_device.{cpp,hpp}` | V4L2 device enumeration, format negotiation, frame capture |
| `video_format.hpp` | `VideoFormat`, `VideoFormatRequest` structs |
| `video_frame.hpp` | `VideoFrameMetadata` IPC user-header definition |
| `video_processor.{cpp,hpp}` | RGA-accelerated YUYV/MJPEG to RGB24 conversion and scaling |
| `video_publisher.{cpp,hpp}` | iceoryx2 publisher wrapper with payload management |
| `rga_context.{cpp,hpp}` | Rockchip RGA API wrapper |

## IPC Data Structures

### VideoFrameMetadata (User Header)

```cpp
struct VideoFrameMetadata {
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t pixel_format;  // kPixelFormatRgb24
    std::uint32_t frame_size_bytes;
    std::uint64_t timestamp_ns;
    std::uint64_t sequence_number;
};
```

### Payload

Raw RGB24 byte array:
- Size: `width × height × 3` bytes
- Layout: Row-major, interleaved RGB
- Byte order: R, G, B per pixel

## Performance Characteristics

- **Zero-copy publishing**: Video frames published directly from RGA output buffer via shared memory
- **Hardware acceleration**: RGA processes YUYV→RGB24 in ~2-5ms at 1080p (50× faster than CPU)
- **Throughput**: Sustained 30 fps at 1080p with <5% CPU usage on single core
- **Latency**: Glass-to-glass latency ~50-70ms (camera → V4L2 → RGA → publish)
- **CPU usage**: ~3-5% on single core during active capture (Cortex-A76 @ 2.4GHz)
- **Memory**: ~12MB for V4L2 buffers + RGA intermediate buffers (4 buffers per stream)

## Troubleshooting

### Device Busy

```bash
# Check if another process is using the camera
sudo lsof /dev/video0
```

### Permission Denied

```bash
# Add user to video group
sudo usermod -a -G video $USER
# Log out and log back in
```

### RGA Initialization Failed

Check that `/dev/rga` exists and has proper permissions:
```bash
ls -l /dev/rga
# Should show: crw-rw---- 1 root video
```

### Unsupported Format

Use `v4l2-ctl` to list supported formats:
```bash
v4l2-ctl -d /dev/video0 --list-formats-ext
```
