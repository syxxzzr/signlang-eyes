# Speech TTS

[简体中文](README.md) | [English](README.en.md)

中文语音合成模块。通过 iceoryx2 请求-响应接口接收文本，使用 Piper 模型（ONNX 编码器 + RKNN 解码器）与 cpp-pinyin 拼音标注合成语音，并通过 ALSA 播放。

## 功能

- 通过 iceoryx2 Request-Response 接收来自 dataflow_dispatcher 的 TTS 请求
- 使用 cpp-pinyin 将中文文本转换为拼音序列
- Piper 编码器在 ONNX Runtime 上运行，解码器在 RK3588 NPU 上运行
- 通过 ALSA PCM 设备播放合成音频
- 支持基于 generation 计数的渐进式取消，确保新请求自动中断旧播放

## 处理管线

```text
iceoryx2 TTS 请求 → SpeechTtsService（任务队列/取消管理）
                        ↓
                  Piper 拼音标注（cpp-pinyin）
                        ↓
                  Piper 编码器（ONNX Runtime / CPU）
                        ↓
                  Piper 解码器（RKNN / NPU）
                        ↓
                  ALSA 播放设备
```

## 命令行参数

| 参数 | 说明 |
| --- | --- |
| `-s, --service <name>` | iceoryx2 TTS 请求-响应服务名（必填） |
| `-d, --device <name>` | ALSA 播放设备（默认 `default`） |
| `--encoder-model <path>` | Piper 编码器 ONNX 模型路径 |
| `--decoder-model <path>` | Piper 解码器 RKNN 模型路径 |
| `--config <path>` | Piper 模型 JSON 配置文件 |
| `--pinyin-dict <dir>` | cpp-pinyin 字典目录 |
| `--npu-core <mask>` | NPU 核心掩码 |
| `--rknn-priority <high|medium|low>` | NPU 优先级 |
| `--cpu-core <n>` | 绑定的 CPU 核心 |

## 并发与取消

`SpeechTtsService` 使用 generation 计数器实现替换式取消：新请求到达时递增 generation，后台合成线程在执行前检查 generation 是否过期，若过期则跳过。ALSA 写入前再次检查，避免播放已被替代的音频。

## 依赖

- ONNX Runtime（编码器推理）
- RKNN API（解码器推理）
- cpp-pinyin（中文拼音标注）
- ALSA（音频播放）
- iceoryx2（Request-Response）
