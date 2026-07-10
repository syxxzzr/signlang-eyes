# Speech ASR

[简体中文](README.md) | [English](README.en.md)

语音识别模块，基于 OpenAI Whisper 架构实现中英文语音转文字。编码器运行在 ONNX Runtime（CPU），解码器运行在 RKNN（NPU），通过滑窗方式实现连续语音识别。

## 功能

- 从 iceoryx2 订阅音频帧（16 kHz 单声道）
- 计算 Mel 频谱特征（80 维滤波器组，使用 FFTW3 加速 FFT）
- Whisper 编码器在 ONNX Runtime 上运行，解码器在 RK3588 NPU 上运行
- 通过 iceoryx2 Publish-Subscribe 发布识别结果
- 当无下游订阅者时暂停推理

## 处理管线

```text
iceoryx2 音频 → AudioRingBuffer → 滑窗提取 → Mel频谱计算（FFTW3）
                                                  ↓
                                          Whisper编码器（ONNX Runtime / CPU）
                                                  ↓
                                          Whisper解码器（RKNN / NPU）
                                                  ↓
                                          SpeechAsrResult → iceoryx2
```

## 命令行参数

| 参数 | 说明 |
| --- | --- |
| `-i, --input-service <name>` | iceoryx2 音频输入服务（必填） |
| `-o, --output-service <name>` | iceoryx2 ASR 结果输出服务（必填） |
| `--language <en|zh>` | 识别语言（默认 `en`） |
| `--encoder-model <path>` | Whisper 编码器 ONNX 模型路径 |
| `--decoder-model <path>` | Whisper 解码器 RKNN 模型路径 |
| `--vocab-en <path>` | 英文词表文件 |
| `--vocab-zh <path>` | 中文词表文件 |
| `--mel-filters <path>` | 80 维 Mel 滤波器文件 |
| `-w, --window-ms <ms>` | ASR 窗口长度（默认 15000 ms） |
| `--overlap <ratio>` | 窗口重叠比例（默认 0.1） |
| `--max-decode-steps <n>` | 解码器最大迭代步数（默认 96） |
| `--subscriber-buffer <n>` | iceoryx2 订阅者队列大小（默认 2） |
| `--npu-core <mask>` | 编码器 NPU 核心掩码 |
| `--decoder-npu-core <mask>` | 解码器 NPU 核心掩码 |
| `--rknn-priority <high|medium|low>` | NPU 优先级 |
| `--cpu-core <n>` | 绑定的 CPU 核心 |

## 架构

双线程设计：接收线程从 iceoryx2 拉取音频写入环形缓冲区，检测线程从缓冲区读取音频窗口执行推理。音频环形缓冲区、Mel 滤波器和编码器输出均使用预分配内存，避免运行时动态分配。

## 依赖

- ONNX Runtime（编码器推理）
- RKNN API（解码器推理）
- FFTW3（频谱计算）
- iceoryx2（Publish-Subscribe）
