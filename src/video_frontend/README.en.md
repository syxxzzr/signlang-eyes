# Video Frontend

[简体中文](README.md) | [English](README.en.md)

V4L2 video capture and processing module. Captures video frames from a camera, performs hardware-accelerated image processing, and publishes RGB24 frames over iceoryx2 publish-subscribe.

## Features

- Captures video frames through the V4L2 MMAP interface (supports YUYV and MJPEG formats)
- Decodes MJPEG in hardware with Rockchip MPP into DMA-backed RGB888 frames
- Uses RGA (Rockchip Graphics Accelerator) for hardware scaling, cropping, rotation, and mirroring
- Passes the MPP output DMA fd directly to RGA without a CPU-side RGB staging buffer
- Publishes processed RGB24 frames over iceoryx2 publish-subscribe
- Automatically pauses capture when no downstream subscribers are connected

## Processing Pipeline

```text
V4L2 device → Capture ─┬─ MJPEG → MPP hardware decode → RGB888 DMA ─┐
                      └─ YUYV MMAP ─────────────────────────────────┤
                                                                   ↓
                               RGA hardware processing (convert/scale/rotate/mirror)
                                                                   ↓
                                                VideoPublisher → iceoryx2
```

The MJPEG path reuses an MPP input buffer and a preallocated, hardware-aligned output frame and buffer, requires `MPP_FMT_RGB888` decoder output, and passes MPP's pixel stride and DMA fd to RGA. Its downstream contract remains synchronous, tightly packed RGB24 output.

## CLI Arguments

| Argument | Description |
| --- | --- |
| `-d, --device <name>` | V4L2 camera device (required) |
| `-s, --service <name>` | iceoryx2 publish-subscribe service name (required) |
| `--capture-width <px>` | Capture width |
| `--capture-height <px>` | Capture height |
| `--output-width <px>` | Output width (default: same as capture) |
| `--output-height <px>` | Output height (default: same as capture) |
| `--fps <rate>` | Capture frame rate (default: 30) |
| `--mirror-output` | Horizontally mirror output |
| `--rotation <0|90|180|270>` | Clockwise rotation in degrees (default: 0) |
| `--cpu-core <n>` | CPU core to pin to |

## Dependencies

- V4L2 (Linux Kernel)
- Rockchip MPP (MJPEG hardware decoding and DMA buffers)
- RGA (`librga`, Rockchip hardware acceleration)
- iceoryx2 (publish-subscribe)

The repository's Conan 2 recipe for `rockchip-mpp/1.0.11` fetches pinned source from Rockchip's official GitHub repository and builds only the JPEG decoding capability required by this project. The target device must provide compatible MPP and RGA kernel drivers.
