# State Machine

[简体中文](README.md) | [English](README.en.md)

应用状态管理模块，维护系统全局状态并提供状态切换控制接口。通过 iceoryx2 事件（Event）、黑板（Blackboard）和请求-响应（Request-Response）三种 IPC 模式对外暴露状态信息与控制能力。

## 状态定义

| 状态 | 类型 | 说明 |
| --- | --- | --- |
| `Normal` | 基础状态 | 默认状态，保持环境声音检测，语音与手语推理空闲 |
| `Asr` | 基础状态 | 启用 Whisper 语音识别，OLED 显示转写结果 |
| `SignLanguageChat` | 基础状态 | 启用手语识别，TTS 播报识别结果 |
| `SignLanguageAi` | 基础状态 | 启用手语识别，累积结果调用 LLM 并显示回复 |
| `DangerousSound` | 特殊状态 | 危险声音触发，带超时自动恢复到基础状态（默认 15 秒） |

基础状态之间可通过 `NextBase` 命令循环切换，也可通过 `SetBase` 直接指定。特殊状态在超时后自动退回到进入前的基础状态，在特殊状态期间基础状态切换请求会被拒绝。

## IPC 接口

| 接口 | 模式 | 服务名 | 说明 |
| --- | --- | --- | --- |
| 状态事件 | Event | `app_state_event` | 每次状态变更时通知所有订阅者 |
| 状态黑板 | Blackboard | `app_state_blackboard` | 供模块随时读取当前状态 |
| 状态控制 | Request-Response | `app_state_control` | 接收状态切换请求并返回执行结果 |

## 状态控制命令

| 命令 | 说明 |
| --- | --- |
| `NextBase` | 切换到下一个基础状态（循环） |
| `SetBase` | 直接设置指定的基础状态 |
| `EnterSpecial` | 进入特殊状态，可指定超时时间（0 表示使用默认值） |

## 命令行参数

| 参数 | 说明 |
| --- | --- |
| `--state-event-service <name>` | iceoryx2 状态事件服务名 |
| `--state-blackboard-service <name>` | iceoryx2 状态黑板服务名 |
| `--state-control-service <name>` | iceoryx2 状态控制服务名 |
| `--initial-state <state>` | 初始状态（默认 `Normal`） |

## 设计要点

- 特殊状态的超时管理基于 `std::chrono::steady_clock`，确保系统时钟调整不影响超时判定
- 状态切换时同时更新黑板数据和发送事件通知，保证订阅者不会丢失状态变更
- 控制响应中携带当前基础状态和特殊状态信息，便于调用方确认执行结果
