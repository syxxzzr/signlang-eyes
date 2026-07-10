# Video Frontend

[简体中文](README.md) | [English](README.en.md)

V4L2 video capture and processing module. Captures video frames from a camera, performs hardware-accelerated image processing, and publishes RGB24 frames over iceoryx2 publish-subscribe.

## Features

- Captures video frames through the V4L2 MMAP interface (supports YUYV and MJPEG formats)
- Uses RGA (Rockchip Graphics Accelerator) for hardware scaling, cropping, rotation, and mirroring
- Decodes MJPEG frames to RGB via libjpeg-turbo
- Publishes processed RGB24 frames over iceoryx2 publish-subscribe
- Automatically pauses capture when no downstream subscribers are connected

## Processing Pipeline

```text
V4L2 device → Capture → MJPEG decode (libjpeg-turbo) / YUYV conversion
                           ↓
                     RGA hardware processing (scale/rotate/mirror)
                           ↓
                     VideoPublisher → iceoryx2
```

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
- RGA (`librga`, Rockchip hardware acceleration)
- libjpeg-turbo (MJPEG decoding)
- iceoryx2 (publish-subscribe)
