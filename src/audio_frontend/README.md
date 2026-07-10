# Audio Frontend

[简体中文](README.md) | [English](README.en.md)

ALSA 音频采集与发布模块，负责从麦克风设备采集 PCM 音频、重采样和声道混音、通过 iceoryx2 发布订阅发布音频帧，并可选地执行声源定位。

## 功能

- 通过 ALSA PCM 接口采集原始音频数据
- 将采集格式（采样率、声道数）转换为统一的发布格式（默认 16 kHz 单声道）
- 通过 iceoryx2 Publish-Subscribe 发布音频帧供下游模块（speech_asr、env_sound_det）消费
- 可选：基于 TDOA + RMS 能量融合的声源定位，结果发布到 iceoryx2 Blackboard
- 当无下游订阅者时自动暂停采集，节省系统资源

## 处理管线

```text
ALSA 设备 → 采集 → AudioProcessor（重采样/混音） → AudioPublisher → iceoryx2
                              ↓
                   SoundSourceLocalizer（TDOA + RMS） → iceoryx2 Blackboard
```

## 命令行参数

| 参数 | 说明 |
| --- | --- |
| `-d, --device <name>` | ALSA 设备名（必填） |
| `-s, --service <name>` | iceoryx2 发布订阅服务名（必填） |
| `--localization-blackboard <name>` | 声源定位 Blackboard 服务名 |
| `--localization-tdoa-weight <float>` | TDOA 权重（默认 0.5） |
| `--localization-rms-weight <float>` | RMS 能量权重（默认 0.5） |
| `-p, --period-ms <ms>` | 音频发布周期（默认 10 ms） |
| `--capture-rate <Hz>` | 采集采样率 |
| `--capture-channels <n>` | 采集声道数 |
| `--publish-rate <Hz>` | 发布采样率（默认 16000） |
| `--publish-channels <n>` | 发布声道数（默认 1） |
| `--cpu-core <n>` | 绑定的 CPU 核心 |

## 声源定位

定位器通过到达时间差（TDOA）和 RMS 能量两个维度融合计算声源接近程度，最终发布归一化的接近度值（0.0 ~ 1.0）。TDOA 和 RMS 权重之和必须为 1.0。

## 依赖

- ALSA (`libasound`)
- iceoryx2（Publish-Subscribe、Blackboard）
- FFTW3（声源定位频域分析）
