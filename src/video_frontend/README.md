# Video Frontend

[简体中文](README.md) | [English](README.en.md)

V4L2 视频采集与处理模块，负责从摄像头设备采集视频帧、执行硬件图像处理，并通过 iceoryx2 发布订阅发布 RGB24 格式的视频帧。

## 功能

- 通过 V4L2 MMAP 接口采集视频帧（支持 YUYV 和 MJPEG 格式）
- 使用 Rockchip MPP 硬件解码 MJPEG，并输出 DMA 支持的 RGB888 帧
- 使用 RGA（Rockchip Graphics Accelerator）执行硬件缩放、裁剪、旋转和镜像
- 将 MPP 输出的 DMA fd 直接交给 RGA，避免 CPU 侧 RGB 中间缓冲
- 通过 iceoryx2 Publish-Subscribe 发布处理后的 RGB24 帧
- 当无下游订阅者时自动暂停采集

## 处理管线

```text
V4L2 设备 → 采集 ─┬─ MJPEG → MPP 硬件解码 → RGB888 DMA ─┐
                  └─ YUYV MMAP ─────────────────────────┤
                                                       ↓
                         RGA 硬件处理（转换/缩放/旋转/镜像）
                                                       ↓
                                    VideoPublisher → iceoryx2
```

MJPEG 路径复用 MPP 输入缓冲和预分配、按硬件要求对齐的输出帧与缓冲，要求解码器输出 `MPP_FMT_RGB888`，并把 MPP 返回的像素步幅与 DMA fd 传给 RGA。模块对下游的同步、紧密排列 RGB24 输出契约保持不变。

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
- Rockchip MPP（MJPEG 硬件解码及 DMA 缓冲）
- RGA（`librga`，Rockchip 硬件加速）
- iceoryx2（Publish-Subscribe）

`rockchip-mpp/1.0.12` 由仓库中的 Conan 2 recipe 从 Rockchip 官方 GitHub 仓库拉取固定提交的源码，并按官方 AArch64 CMake 流程构建 MPP 共享库（本项目使用其中的 JPEG 解码能力）。目标设备必须提供兼容的 MPP 与 RGA 内核驱动。
