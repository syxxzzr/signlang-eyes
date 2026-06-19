# state_machine — Global Application State Controller

## Overview

The **state_machine** module is the central application state controller for the sign language edge AI system. It manages the global application state (Normal, ASR, SignLanguageChat, SignLanguageAi, DangerousSound) and distributes state changes to all other modules via iceoryx2 Event + Blackboard notification. It also accepts state change requests from other modules via a Request-Response service, with special-state timeout handling.

- **Executable**: `state_machine` (installed under `bin/`)
- **IPC Pattern**: Event (state change notification) + Blackboard (state storage) + Request-Response (state control)
- **Input**: `StateControlRequest` via iceoryx2 Request-Response
- **Output**: `AppState` on Event + Blackboard services

## File Inventory

| File | Description |
|------|-------------|
| `main.cpp` | Entry point; signal handling, main control loop (100 ms cycle) |
| `program_options.{cpp,hpp}` | CLI argument parsing via cxxopts |
| `app_state.{cpp,hpp}` | `AppState` enum (5 states), `AppStateKey` struct, helper functions |
| `state_control.{cpp,hpp}` | `StateController` class: state machine logic, base/special state transitions, timeout handling |
| `iceoryx_gateway.{cpp,hpp}` | iceoryx2 Event notifier, Blackboard publisher, Request-Response server |

## Application States

| State | Value | Type | Description |
|-------|-------|------|-------------|
| `Normal` | `0` | Base | Default idle state; ASR and sign-language inference modules remain disabled until their active states are selected |
| `Asr` | `1` | Base | Speech recognition mode; ASR module prioritized |
| `SignLanguageChat` | `2` | Base | Sign language chat mode; sign language recognition active |
| `SignLanguageAi` | `3` | Base | Sign language AI interaction mode |
| `DangerousSound` | `4` | Special | Alert state; triggered by env_sound_det on horn/siren detection. Auto-expires after timeout (default 15 s) |

- **Base states**: Mutually exclusive; transitioning to a new base state replaces the current one
- **Special states**: Overlay on top of base states; auto-expire after `timeout_ms`; the base state persists underneath

## Command-Line Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--state-event-service` | *(required)* | iceoryx2 event service name for app state change notifications |
| `--state-blackboard-service` | *(required)* | iceoryx2 blackboard service name for app state storage |
| `--state-control-service` | *(required)* | iceoryx2 request-response service name for state control commands |
| `--help` / `-h` | — | Print usage |

## Technical Details

### State Control Commands

Other modules can send `StateControlRequest` messages with these commands:

| Command | Value | Description |
|---------|-------|-------------|
| `NextBase` | `0` | Cycle to the next base state (Normal → ASR → SignLanguageChat → SignLanguageAi → Normal) |
| `SetBase` | `1` | Set a specific base state (target in `target_state` field) |
| `EnterSpecial` | `2` | Enter a special state (target in `target_state` field, timeout in `timeout_ms`); rejected during another special state |

### Response Codes

| Error Code | Description |
|------------|-------------|
| `None` (`0`) | Command accepted; state changed successfully |
| `InvalidTargetState` (`1`) | Target state is not valid for the given command |
| `IgnoredDuringSpecialState` (`2`) | Command ignored because a special state is currently active |
| `InvalidCommand` (`3`) | Unrecognized or malformed command |

### Main Control Loop (100 ms cycle)

1. Check if the current special state has expired (`expire_special_state()`)
2. If expired, revert to the base state and publish the change
3. Process pending state control requests from other modules
4. Sleep for 100 ms

### IPC Integration

- **Event Service**: When state changes, a `notify()` is sent to wake up other modules blocked on the event FD
- **Blackboard Service**: Stores the current `AppState` value; other modules read it after receiving the event notification
- **Request-Response Server**: Listens for `StateControlRequest` messages, processes them via `StateController`, sends back `StateControlResponse`

### Module Integration Flow

```
state_machine                              other modules
     │                                           │
     │  ── Event.notify() ───────────────────→  │  (wake up from blocking wait)
     │  ── Blackboard.write(state) ──────────→  │  (read current state)
     │                                           │
     │  ←── Request-Response: request ────────  │  (env_sound_det sends horn alert)
     │  ── Request-Response: response ───────→  │
     │  ── Event.notify() ───────────────────→  │  (notify all modules of state change)
```

## Usage Example

```bash
# Start the state machine (typically first in the startup sequence)
./state_machine \
    --state-event-service app_state_event \
    --state-blackboard-service app_state_blackboard \
    --state-control-service app_state_control
```

## Startup Sequence

The state_machine should be started before other modules that depend on it:

```bash
# 1. Start state machine
./state_machine \
    --state-event-service app_state_event \
    --state-blackboard-service app_state_blackboard \
    --state-control-service app_state_control &

# 2. Start audio/video frontends
./audio_frontend --device hw:0,0 --service audio_capture &
./video_frontend --device /dev/video0 --service video_capture &

# 3. Start inference modules (each references the same state services)
./speech_asr \
    --input-service audio_capture --output-service speech_asr_result \
    --state-event-service app_state_event --state-blackboard-service app_state_blackboard &

./env_sound_det \
    --input-service audio_capture --output-service env_sound_result \
    --state-control-service app_state_control &

./handpose_det \
    --input-service video_capture --output-service handpose_result \
    --state-event-service app_state_event --state-blackboard-service app_state_blackboard &

./signlang_det \
    --input-service handpose_result --output-service signlang_result \
    --state-event-service app_state_event --state-blackboard-service app_state_blackboard &
```
