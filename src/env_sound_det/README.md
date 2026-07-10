# Environmental Sound Detection

[简体中文](README.md) | [English](README.en.md)

环境声音识别模块。基于 YAMNet 模型在 RK3588 NPU 上进行环境声音分类，识别危险声音（如车辆喇叭）并触发应用危险状态。

## 功能

- 从 iceoryx2 订阅音频帧（16 kHz 单声道）
- 使用 YAMNet 模型在 RKNN NPU 上进行推理
- 检测预定义的危险声音类别：气喇叭/卡车喇叭、车辆喇叭/汽车喇叭/鸣笛、火车喇叭
- 检测到危险声音时通过 iceoryx2 状态控制接口触发 `DangerousSound` 状态
- 采用滑窗方式实现连续检测，窗口默认 3 秒

## 处理管线

```text
iceoryx2 音频 → AudioRingBuffer → 滑窗提取 → YAMNet（RKNN / NPU）
                                                  ↓
                                          危险标签匹配 → state_control.EnterSpecial(DangerousSound)
```

## 命令行参数

| 参数 | 说明 |
| --- | --- |
| `-i, --input-service <name>` | iceoryx2 音频输入服务（必填） |
| `--state-control-service <name>` | iceoryx2 状态控制服务（必填） |
| `-m, --model <path>` | YAMNet RKNN 模型路径 |
| `--class-map <path>` | YAMNet 类别标签文件 |
| `-w, --window-ms <ms>` | 检测窗口长度（默认 3000 ms） |
| `--overlap <ratio>` | 窗口重叠比例（默认 0.2） |
| `--score-threshold <0.0-1.0>` | 检测置信度阈值（默认 0.3） |
| `--subscriber-buffer <n>` | iceoryx2 订阅者队列大小（默认 2） |
| `--npu-core <mask>` | NPU 核心掩码 |
| `--rknn-priority <high|medium|low>` | NPU 优先级 |

## 设计要点

- YAMNet 输入为 16 kHz 单声道音频，系统会检测与 audio_frontend 发布格式的兼容性
- 双线程架构：接收线程缓冲音频，检测线程执行推理
- 采样率不匹配时会发出警告，避免缓冲区饥饿
- 每个窗口独立检测，score_threshold 可调节灵敏度

## 依赖

- RKNN API（推理）
- iceoryx2（Publish-Subscribe、Request-Response）
