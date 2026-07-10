# Dataflow Dispatcher

[简体中文](README.md) | [English](README.en.md)

Central dataflow routing hub. Subscribes to the system state and detection results, routing data to the appropriate downstream services (TTS, LLM, peripheral display) based on the current application state.

## Features

- Tracks the current application state via iceoryx2 event + blackboard
- Determines which upstream data source to subscribe to based on state (None / `signlang_det` / `speech_asr`)
- Executes per-state routing logic: Normal displays title, Asr displays transcription, SignLanguageChat speaks via TTS, SignLanguageAi calls LLM, DangerousSound shows warning
- Accumulates sign language results in SignLanguageAi mode and triggers LLM requests after an idle timeout
- Updates the OLED title line on state transitions

## Routing Logic

| State | Upstream Source | Downstream Action |
| --- | --- | --- |
| `Normal` | None | Display title line |
| `Asr` | speech_asr | Display transcription on OLED |
| `SignLanguageChat` | signlang_det | Speak recognized gesture via TTS |
| `SignLanguageAi` | signlang_det | Accumulate results, flush to LLM after idle timeout, display reply |
| `DangerousSound` | None | Display danger warning title, enable vibration |

## CLI Arguments

| Argument | Description |
| --- | --- |
| `--state-event-service <name>` | iceoryx2 state event service (required) |
| `--state-blackboard-service <name>` | iceoryx2 state blackboard service (required) |
| `--signlang-result-service <name>` | iceoryx2 sign language result service (required) |
| `--speech-asr-result-service <name>` | iceoryx2 ASR result service (required) |
| `--speech-tts-service <name>` | iceoryx2 TTS service (required) |
| `--llm-client-service <name>` | iceoryx2 LLM client service (required) |
| `--peripheral-display-service <name>` | iceoryx2 peripheral display service (required) |
| `--subscriber-buffer <n>` | iceoryx2 subscriber queue size (default: 2) |
| `--signlang-ai-window-ms <ms>` | AI idle timeout (default: 5000 ms) |

## SignLanguageAi Accumulation

The `SignlangAiAccumulator` continuously collects sign language recognition text fragments. When no new sign language result arrives within `signlang_ai_window_ms`, the accumulated text is concatenated into a complete prompt and sent to the LLM. This mechanism ensures consecutive sign language gestures are assembled into a single coherent sentence rather than sent word-by-word.

## Dependencies

- iceoryx2 (event, blackboard, publish-subscribe, request-response)
