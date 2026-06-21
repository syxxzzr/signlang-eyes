# state_machine — Global Application State Controller

## Overview

The **state_machine** module is the central application state controller for the sign language edge AI system. It manages the global application state (Normal, ASR, SignLanguageChat, SignLanguageAi, DangerousSound) and distributes state changes to all other modules via iceoryx2 Event + Blackboard notification. It also accepts state change requests from other modules via a Request-Response service, with special-state timeout handling.

- **Executable**: `state_machine` (installed under `bin/`)
- **IPC Pattern**: Event (state change notification) + Blackboard (state storage) + Request-Response (state control)
- **Input**: `StateControlRequest` via iceoryx2 Request-Response
- **Output**: `AppState` on Event + Blackboard services

## Application States

| State | Value | Type | Description |
|-------|-------|------|-------------|
| `Normal` | `0` | Base | Default idle state; ASR and sign-language inference modules remain disabled |
| `Asr` | `1` | Base | Speech recognition mode; ASR module active |
| `SignLanguageChat` | `2` | Base | Sign language chat mode; sign language recognition active |
| `SignLanguageAi` | `3` | Base | Sign language AI interaction mode |
| `DangerousSound` | `4` | Special | Alert state; triggered by env_sound_det on horn/siren detection. Auto-expires after timeout (default 15s) |

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

### Main Control Loop (100ms cycle)

1. Check if the current special state has expired (`expire_special_state()`)
2. If expired, revert to the base state and publish the change
3. Process pending state control requests from other modules
4. Sleep for 100ms

### IPC Integration

- **Event Service**: When state changes, a `notify()` is sent to wake up other modules blocked on the event FD
- **Blackboard Service**: Stores the current `AppState` value; other modules read it after receiving the event notification
- **Request-Response Server**: Listens for `StateControlRequest` messages, processes them via `StateController`, sends back `StateControlResponse`

### Module Integration Flow

```
state_machine                              other modules
     │                                           │
     │  ── Event.notify() ───────────────────→  │  (wake up from event)
     │  ── Blackboard.write(state) ──────────→  │  (read current state)
     │                                           │
     │  ←── Request-Response: request ────────  │  (env_sound_det sends horn alert)
     │  ── Request-Response: response ───────→  │
     │  ── Event.notify() ───────────────────→  │  (notify all modules of state change)
```

## Architecture

```
Request-Response Server
(state control requests)
        │
        ▼
StateController
(state machine logic)
        │
        ├─► Base State Transitions
        │   (Normal/Asr/SignLanguageChat/SignLanguageAi)
        │
        ├─► Special State Handling
        │   (DangerousSound with timeout)
        │
        └─► Timeout Expiration
                │
                ▼
        Event Notifier
        (state change events)
                │
                ▼
        Blackboard Publisher
        (current state storage)
```

## Dependencies

- **iceoryx2**: Zero-copy IPC (Event, Blackboard, Request-Response)
- **spdlog**: Logging

## Usage Examples

### Basic Usage

```bash
# Start the state machine (typically first in the startup sequence)
./state_machine \
    --state-event-service app_state_event \
    --state-blackboard-service app_state_blackboard \
    --state-control-service app_state_control
```

### Full System Startup Sequence

The state_machine should be started before other modules that depend on it:

```bash
# 1. Start state machine
./state_machine \
    --state-event-service app_state_event \
    --state-blackboard-service app_state_blackboard \
    --state-control-service app_state_control &

# 2. Start audio/video frontends (no state dependency)
./audio_frontend --device hw:0,0 --service audio_capture &
./video_frontend --device /dev/video0 --service video_capture &

# 3. Start inference modules (each references the same state services)
./speech_asr \
    --input-service audio_capture --output-service speech_asr_result \
    --state-event-service app_state_event --state-blackboard-service app_state_blackboard &

./env_sound_det \
    --input-service audio_capture \
    --state-control-service app_state_control &

./handpose_det \
    --input-service video_capture --output-service handpose_result \
    --state-event-service app_state_event --state-blackboard-service app_state_blackboard &

./signlang_det \
    --input-service handpose_result --output-service signlang_result \
    --state-event-service app_state_event --state-blackboard-service app_state_blackboard &
```

## File Organization

| File | Description |
|------|-------------|
| `main.cpp` | Entry point; signal handling, main control loop (100ms cycle) |
| `program_options.{cpp,hpp}` | CLI argument parsing via cxxopts |
| `app_state.{cpp,hpp}` | `AppState` enum (5 states), `AppStateKey` struct, helper functions |
| `state_control.{cpp,hpp}` | `StateController` class: state machine logic, base/special state transitions, timeout handling |
| `iceoryx_gateway.{cpp,hpp}` | iceoryx2 Event notifier, Blackboard publisher, Request-Response server |

## IPC Data Structures

### AppState (Published to Blackboard)

```cpp
enum class AppState : std::uint32_t {
    Normal = 0,
    Asr = 1,
    SignLanguageChat = 2,
    SignLanguageAi = 3,
    DangerousSound = 4
};

struct AppStateKey {
    std::uint32_t id;  // Always 0 (single-entry blackboard)
};
```

### StateControlRequest (Request-Response)

```cpp
enum class StateControlCommand : std::uint32_t {
    NextBase = 0,
    SetBase = 1,
    EnterSpecial = 2
};

struct StateControlRequest {
    StateControlCommand command;
    AppState target_state;
    std::uint32_t timeout_ms;  // For EnterSpecial only
};
```

### StateControlResponse (Request-Response)

```cpp
enum class StateControlError : std::uint32_t {
    None = 0,
    InvalidTargetState = 1,
    IgnoredDuringSpecialState = 2,
    InvalidCommand = 3
};

struct StateControlResponse {
    StateControlError error;
};
```

## State Transition Examples

### Cycle Base States

```cpp
// Client sends: NextBase command
StateControlRequest request{
    .command = StateControlCommand::NextBase,
    .target_state = AppState::Normal,  // Ignored for NextBase
    .timeout_ms = 0
};
```

### Set Specific Base State

```cpp
// Client sends: SetBase command to enter ASR mode
StateControlRequest request{
    .command = StateControlCommand::SetBase,
    .target_state = AppState::Asr,
    .timeout_ms = 0
};
```

### Enter Special State (Dangerous Sound)

```cpp
// Client sends: EnterSpecial command with 15s timeout
StateControlRequest request{
    .command = StateControlCommand::EnterSpecial,
    .target_state = AppState::DangerousSound,
    .timeout_ms = 15000
};
```

## Performance Characteristics

- **Control loop frequency**: 10 Hz (100ms period)
- **Event notification latency**: <1ms
- **Memory**: <1MB resident
- **CPU usage**: <1% on single core
- **Special state timeout precision**: ±100ms (limited by control loop frequency)

## Module State Integration

### State-Aware Modules

Modules that subscribe to state events and adjust behavior:

| Module | States When Active | Behavior When Disabled |
|--------|-------------------|------------------------|
| `speech_asr` | Asr | Skips inference, event-driven idle |
| `handpose_det` | SignLanguageChat, SignLanguageAi | Skips inference, event-driven idle |
| `signlang_det` | SignLanguageChat, SignLanguageAi | Skips inference, event-driven idle |

### State-Triggering Modules

Modules that send state control requests:

| Module | Request Type | Trigger Condition |
|--------|--------------|-------------------|
| `env_sound_det` | EnterSpecial (DangerousSound) | Horn/siren detected above threshold |

## Troubleshooting

### Modules Not Responding to State Changes

Check that all modules are using the same service names:
```bash
# Should show app_state_event, app_state_blackboard, app_state_control
iox2-list
```

### State Changes Not Propagating

Check state_machine logs:
```bash
# Should show state transition messages
tail -f logs/state_machine.log
```

### Special State Not Expiring

- Check timeout value in EnterSpecial request
- Verify control loop is running (should log every state change)
- Special state expiration precision is ±100ms

### Request-Response Timeout

Ensure state_machine is running before modules try to send requests:
```bash
ps aux | grep state_machine
```
