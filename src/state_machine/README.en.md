# State Machine

[简体中文](README.md) | [English](README.en.md)

Application state management module. Maintains the global system state and provides a state-switching control interface. Exposes state information and control capabilities through three iceoryx2 IPC patterns: Event, Blackboard, and Request-Response.

## State Definitions

| State | Type | Description |
| --- | --- | --- |
| `Normal` | Base state | Default state; environmental sound detection active, speech and sign language inference idle |
| `Asr` | Base state | Whisper speech recognition enabled; transcription displayed on OLED |
| `SignLanguageChat` | Base state | Sign language recognition enabled; results spoken via TTS |
| `SignLanguageAi` | Base state | Sign language recognition enabled; accumulated results sent to LLM, reply displayed |
| `DangerousSound` | Special state | Triggered by dangerous sound detection; auto-recovers to the base state after timeout (default 15 s) |

Base states cycle via the `NextBase` command or can be set directly with `SetBase`. A special state automatically reverts to the previous base state on timeout. Base-state switch requests are rejected while a special state is active.

## IPC Interfaces

| Interface | Pattern | Service Name | Description |
| --- | --- | --- | --- |
| State Event | Event | `app_state_event` | Notifies all subscribers on every state change |
| State Blackboard | Blackboard | `app_state_blackboard` | Allows modules to read the current state at any time |
| State Control | Request-Response | `app_state_control` | Accepts state-switch requests and returns execution results |

## State Control Commands

| Command | Description |
| --- | --- |
| `NextBase` | Cycle to the next base state |
| `SetBase` | Set a specific base state directly |
| `EnterSpecial` | Enter a special state with an optional timeout (0 uses the default) |

## CLI Arguments

| Argument | Description |
| --- | --- |
| `--state-event-service <name>` | iceoryx2 state event service name |
| `--state-blackboard-service <name>` | iceoryx2 state blackboard service name |
| `--state-control-service <name>` | iceoryx2 state control service name |
| `--initial-state <state>` | Initial state (default: `Normal`) |

## Design Notes

- Special-state timeout management is based on `std::chrono::steady_clock`, ensuring immunity to system clock adjustments
- State transitions update the blackboard and emit an event notification atomically, so subscribers never miss a change
- Control responses carry both the current base state and special state information, enabling callers to verify the outcome
