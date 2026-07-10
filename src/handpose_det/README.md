# Hand Pose Detection

[简体中文](README.md) | [English](README.en.md)

手部关键点检测与跟踪模块。基于 MediaPipe 架构，在 RK3588 NPU 上运行手掌检测和手部关键点（21 点）模型，支持双手跟踪、平滑滤波和左右手判别。

## 功能

- 从 iceoryx2 订阅视频帧（RGB24）
- 两阶段推理：手掌检测器定位手掌区域 → 关键点模型提取 21 个手部关键点
- 支持双手检测（可通过 `--single-hand` 限制为单手）
- 基于 IoU 的跟踪：优先使用上一帧 ROI 进行快速跟踪，周期性执行全帧检测
- One Euro Filter 平滑关键点抖动
- 左右手判别
- 通过 iceoryx2 Publish-Subscribe 发布检测结果

## 处理管线

```text
VideoFrame → PalmDetector（RKNN）→ NMS → LandmarkDetector（RKNN）
                                              ↓
                                        Tracking & Smoothing（One Euro Filter）
                                              ↓
                                        HandPoseDetection[] → iceoryx2
```

## 命令行参数

| 参数 | 说明 |
| --- | --- |
| `-i, --input-service <name>` | iceoryx2 视频输入服务（必填） |
| `-o, --output-service <name>` | iceoryx2 手部结果输出服务（必填） |
| `-m, --model <path>` | 手掌检测 RKNN 模型路径 |
| `--landmark-model <path>` | 关键点检测 RKNN 模型路径 |
| `--confidence <float>` | 手掌检测置信度阈值（默认 0.5） |
| `--presence-threshold <float>` | 手部存在置信度（默认 0.5） |
| `--tracking-threshold <float>` | 跟踪置信度阈值（默认 0.5） |
| `--nms-iou-threshold <float>` | NMS IoU 阈值（默认 0.3） |
| `--tracking-iou-match <float>` | 跟踪匹配 IoU 阈值（默认 0.3） |
| `--crop-expansion <float>` | 裁剪框扩展系数（默认 2.0） |
| `--base-roi-expansion <float>` | 基础 ROI 扩展系数（默认 2.6） |
| `--small-hand-expansion <float>` | 小手额外扩展（默认 2.0） |
| `--large-hand-expansion <float>` | 大手额外扩展（默认 0.0） |
| `--small-hand-threshold <float>` | 小手判定阈值（默认 0.05） |
| `--large-hand-threshold <float>` | 大手判定阈值（默认 0.15） |
| `--euro-min-cutoff <Hz>` | One Euro Filter 最小截止频率 |
| `--euro-beta <float>` | One Euro Filter 速度系数 |
| `--euro-d-cutoff <Hz>` | One Euro Filter 微分截止频率 |
| `--handedness-threshold <float>` | 左右手阈值（默认 0.5） |
| `--swap-handedness` | 交换左右手（镜像摄像头用） |
| `--max-tracking-gap <frames>` | 最大跟踪中断帧数 |
| `--max-stale-frames <frames>` | 最大陈旧帧数 |
| `--full-frame-interval <frames>` | 全帧检测间隔（0 禁用） |
| `--single-hand` | 仅识别单手 |
| `--npu-core <mask>` | 手掌模型 NPU 核心 |
| `--palm-npu-core <mask>` | 手掌模型 NPU 核心（覆盖 --npu-core） |
| `--landmark-npu-core <mask>` | 关键点模型 NPU 核心 |
| `--subscriber-buffer <n>` | iceoryx2 订阅者队列大小 |

## 设计要点

- 默认每 10 帧执行一次全帧手掌检测，其余帧通过 ROI 跟踪实现低延迟
- NMS（非极大值抑制）消除重叠检测框
- One Euro Filter 提供自适应平滑：低速时强平滑，高速时快速跟随
- 基于手掌关键点的手部边界裁剪，兼顾尺寸自适应和边界保护
- DMA 缓冲区管理确保 RK3588 NPU 高效数据访问

## 依赖

- RKNN API（推理）
- RGA（DMA 缓冲区管理）
- iceoryx2（Publish-Subscribe）
