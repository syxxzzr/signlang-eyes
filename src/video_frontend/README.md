# Video Frontend

[简体中文](README.md) | [English](README.en.md)

V4L2 视频采集与处理模块，负责从摄像头设备采集视频帧、执行硬件图像处理，并通过 iceoryx2 发布订阅发布 RGB24 格式的视频帧。

## 功能

- 通过 V4L2 MMAP 接口采集视频帧（支持 YUYV 和 MJPEG 格式）
- 使用 RGA（Rockchip Graphics Accelerator）执行硬件缩放、裁剪、旋转和镜像
- 使用 libjpeg-turbo 解码 MJPEG 帧为 RGB
- 通过 iceoryx2 Publish-Subscribe 发布处理后的 RGB24 帧
- 当无下游订阅者时自动暂停采集

## 处理管线

```text
V4L2 设备 → 采集 → MJPEG解码（libjpeg-turbo）/ YUYV转换
                      ↓
                RGA 硬件处理（缩放/旋转/镜像）
                      ↓
                VideoPublisher → iceoryx2
```

## 命令行参数

| 参数 | 说明 |
| --- | --- |
| `-d, --device <name>` | V4L2 摄像头设备（必填） |
| `-s, --service <name>` | iceoryx2 发布订阅服务名（必填） |
| `--capture-width <px>` | 采集宽度 |
| `--capture-height <px>` | 采集高度 |
| `--output-width <px>` | 输出宽度（默认等于采集宽度） |
| `--output-height <px>` | 输出高度（默认等于采集高度） |
| `--fps <rate>` | 采集帧率（默认 30） |
| `--mirror-output` | 水平镜像输出 |
| `--rotation <0|90|180|270>` | 顺时针旋转角度（默认 0） |
| `--cpu-core <n>` | 绑定的 CPU 核心 |

## 依赖

- V4L2（Linux Kernel）
- RGA（`librga`，Rockchip 硬件加速）
- libjpeg-turbo（MJPEG 解码）
- iceoryx2（Publish-Subscribe）
