# Dataflow Dispatcher

[简体中文](README.md) | [English](README.en.md)

数据流调度中枢。订阅系统状态和检测结果，根据当前应用状态将数据路由到对应的下游服务（TTS、LLM、外设显示）。

## 功能

- 通过 iceoryx2 Event + Blackboard 跟踪当前应用状态
- 根据状态决定需要订阅的上游数据源（None / signlang_det / speech_asr）
- 按状态执行路由逻辑：Normal 显示标题、Asr 转写显示、SignLanguageChat 语音播报、SignLanguageAi 调用 LLM、DangerousSound 告警显示
- SignLanguageAi 状态下的手语结果累积与空闲超时触发 LLM 请求
- 状态切换时更新 OLED 标题行

## 路由逻辑

| 状态 | 上游数据源 | 下游操作 |
| --- | --- | --- |
| `Normal` | 无 | 显示标题行 |
| `Asr` | speech_asr | 将转写文字显示到 OLED |
| `SignLanguageChat` | signlang_det | TTS 播报识别结果 |
| `SignLanguageAi` | signlang_det | 累积手语结果，空闲超时后发送 LLM 请求，显示回复 |
| `DangerousSound` | 无 | 显示危险警告标题，启用振动 |

## 命令行参数

| 参数 | 说明 |
| --- | --- |
| `--state-event-service <name>` | iceoryx2 状态事件服务（必填） |
| `--state-blackboard-service <name>` | iceoryx2 状态黑板服务（必填） |
| `--signlang-result-service <name>` | iceoryx2 手语识别结果服务（必填） |
| `--speech-asr-result-service <name>` | iceoryx2 ASR 结果服务（必填） |
| `--speech-tts-service <name>` | iceoryx2 TTS 服务（必填） |
| `--llm-client-service <name>` | iceoryx2 LLM 客户端服务（必填） |
| `--peripheral-display-service <name>` | iceoryx2 外设显示服务（必填） |
| `--subscriber-buffer <n>` | iceoryx2 订阅者队列大小（默认 2） |
| `--signlang-ai-window-ms <ms>` | AI 空闲超时（默认 5000 ms） |

## SignLanguageAi 累积机制

`SignlangAiAccumulator` 持续收集手语识别结果文本片段。当在 `signlang_ai_window_ms` 时间段内没有新的手语结果到达时，将累积文本拼接为完整提示词发送给 LLM。此机制确保连续的多个手语动作被拼合成一个完整语句，而非逐词发送。

## 依赖

- iceoryx2（Event、Blackboard、Publish-Subscribe、Request-Response）
