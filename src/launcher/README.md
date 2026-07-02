# launcher — System Orchestrator

## Overview

The **launcher** module is the system entry point that reads per-module configuration from a TOML file and spawns all 8 sub-modules as child processes. It monitors child health via `waitpid()` and performs coordinated shutdown of the entire system on SIGINT/SIGTERM or if any module exits unexpectedly.

- **Executable**: `launcher` (installed at root level, not under `bin/`)
- **Input**: TOML configuration file (`conf/conf.toml` by default)
- **Output**: Spawns and supervises all child modules with health monitoring and configurable restart attempts

## Command-Line Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--config` / `-c` | `conf/conf.toml` | Path to the TOML configuration file |
| `--help` / `-h` | — | Print usage |

The launcher changes its working directory to the installation root before reading default paths, starting children, and
creating logs. If `--config` is a relative path, it is resolved from the caller's original working directory when that
file exists; otherwise it is resolved from the installation root.

## Configuration File

The TOML file has one `[section]` per module. All keys are optional — omitted keys fall back to each module's built-in default. IPC service names are **hardcoded** in the launcher and cannot be configured.

See `conf/conf.toml` for the default configuration with all available keys documented as comments.

### Configuration Sections

- `[launcher]` — Launcher supervision configuration (restart_attempts)
- `[logging]` — Global logging configuration (rotate_size, retain_files)
- `[audio_frontend]` — Audio capture parameters (device, capture_rate, capture_channels, etc.)
- `[video_frontend]` — Video capture parameters (device, output_width, output_height, cpu_core, etc.)
- `[speech_asr]` — Whisper ASR parameters (language, npu_core, cpu_core, window_ms, etc.)
- `[env_sound_det]` — YAMNet environmental sound detection parameters (npu_core, score_threshold, etc.)
- `[handpose_det]` — MediaPipe hand pose detection parameters (npu_core, confidence, single_hand, etc.)
- `[signlang_det]` — Sign language recognition parameters (npu_core, sequence_length, confidence_threshold, etc.)
- `[signlang_manager]` — BLE GATT streaming and prototype database management parameters
- `[llm_client]` — OpenAI-compatible LLM request service parameters

## Technical Details

### IPC Service Names (Hardcoded)

The launcher hardcodes all IPC service names. Defining IPC keys (e.g., `input_service`) in the TOML emits a warning and the value is ignored.

```
audio_capture          → audio_frontend → speech_asr, env_sound_det
video_capture          → video_frontend → handpose_det
speech_asr_result      ← speech_asr
handpose_result        ← handpose_det → signlang_det
signlang_result        ← signlang_det
app_state_event        ↔ state_machine
app_state_blackboard   ↔ state_machine
app_state_control      ↔ state_machine ← env_sound_det
signlang_prototype_control ↔ signlang_manager → signlang_det
audio_source_localization ↔ audio_frontend (sound source localization blackboard)
llm_client           ↔ OpenAI-compatible LLM request-response service
```

### Startup Order

Modules are started sequentially in this order:

1. `state_machine` — global state controller (must start first)
2. `audio_frontend` — audio capture
3. `video_frontend` — video capture
4. `speech_asr` — speech recognition
5. `env_sound_det` — environmental sound detection
6. `handpose_det` — hand keypoint detection
7. `signlang_manager` — BLE streaming and prototype database management
8. `signlang_det` — sign language recognition
9. `llm_client` — OpenAI-compatible LLM request-response service

### Process Lifecycle

- **Launch**: `fork()` + `execvp()`. A `pipe2(…, O_CLOEXEC)` detects exec failures — if the child writes back `errno`, the parent knows the exec failed and aborts the entire launch
- **Monitor**: `waitpid(-1, &status, WNOHANG)` in a 500ms loop. On any child exit (normal or abnormal), all remaining children receive `SIGTERM`, then the launcher restarts the full module set if attempts remain
- **Shutdown**: `SIGINT`/`SIGTERM` on the launcher itself triggers `SIGTERM` to all children, then `waitpid()` to reap them gracefully
- **Restart attempts**: `[launcher].restart_attempts` controls automatic full-system restarts after module startup/runtime failure. The default is `-1`, and any value `< 0` means unlimited restarts.

**Error handling:**
- Exec failure: Detected via close-on-exec pipe, launcher terminates already-started siblings and enters the restart policy
- Child crash: Detected via `waitpid()`, all siblings receive `SIGTERM`, then the restart policy decides whether to relaunch or exit non-zero
- Missing executable: Caught at exec time, errno propagated to parent via pipe

### TOML Parsing

- Each module section is read independently; missing sections are allowed and the module still starts with its own defaults
- String, integer, floating-point, and boolean values are supported via `toml++` accessors
- Numeric keys use underscore naming (`capture_rate`, `window_ms`) and are mapped to each module's CLI flags (`--capture-rate`, `--window-ms`)
- Array and table values are not currently supported

### Argument Building

For each module, the launcher:
1. Reads the corresponding TOML section
2. Maps TOML keys to CLI arguments
3. Adds hardcoded IPC service names
4. Constructs the `argv[]` array for `execvp()`

Example mapping:
```toml
[speech_asr]
language = "zh"
window_ms = 15000
npu_core = "1"
cpu_core = 1
```

Becomes:
```bash
bin/speech_asr --input-service audio_capture \
               --output-service speech_asr_result \
               --language zh \
               --window-ms 15000 \
               --npu-core 1 \
               --cpu-core 1
```

## Architecture

```
launcher
    │
    ├─► Read conf/conf.toml
    │
    ├─► Fork & Exec Modules
    │   ├─► state_machine
    │   ├─► audio_frontend
    │   ├─► video_frontend
    │   ├─► speech_asr
    │   ├─► env_sound_det
    │   ├─► handpose_det
    │   ├─► signlang_manager
    │   └─► signlang_det
    │
    └─► Monitor Loop (500ms)
        ├─► Wait for child exit
        ├─► On any exit → SIGTERM all
        └─► Clean shutdown
```

## Dependencies

- **cxxopts**: CLI argument parsing (header-only)
- **toml++**: TOML configuration parsing (header-only)
- **POSIX**: `fork`, `execvp`, `waitpid`, `kill`, `pipe2`, `nanosleep`
- **spdlog**: Logging (shared library)

## Install Layout

```
install/
├── launcher                     ← this executable (root, not bin/)
├── bin/
│   ├── state_machine
│   ├── audio_frontend
│   ├── video_frontend
│   ├── speech_asr
│   ├── env_sound_det
│   ├── handpose_det
│   ├── signlang_manager
│   └── signlang_det
├── conf/
│   ├── conf.toml
│   └── prototypes.sqlite
├── lib/
│   ├── libiceoryx2_cxx.so
│   ├── libiceoryx2_ffi_c.so
│   ├── librknnrt.so
│   ├── libspdlog.so
│   └── librga.so
├── log/                         ← auto-created on first run
└── models/
    ├── whisper/
    ├── yamnet/
    ├── mediapipe/
    └── bilstm/
```

## Usage Examples

### Basic Usage

```bash
# Start all modules with default configuration
/opt/signlang-eyes/launcher
```

### Custom Configuration

```bash
# Use a custom configuration file
/opt/signlang-eyes/launcher --config /etc/signlang/config.toml
```

### Check Module Status

```bash
# List iceoryx2 services (requires iox2-list from iceoryx2)
iox2-list

# Check running processes
ps aux | grep signlang
```

## File Organization

| File | Description |
|------|-------------|
| `main.cpp` | Entry point; TOML parsing, argument building, fork+exec, child monitoring |
| `program_options.{cpp,hpp}` | CLI argument parsing via cxxopts |

## Configuration Example

Minimal `conf.toml` with most common settings:

```toml
[logging]
rotate_size = 1048576      # 1MB per log file
retain_files = 100         # Keep 100 old log files

[audio_frontend]
device = "hw:2,0"          # ALSA device
capture_rate = 16000       # 16 kHz
capture_channels = 2       # Stereo

[video_frontend]
device = "/dev/video21"    # V4L2 camera
output_width = 640         # Published width
output_height = 480        # Published height
mirror_output = false      # Horizontally mirror published frames
# cpu_core = 0              # Optional CPU core binding

[speech_asr]
language = "zh"            # Chinese recognition
npu_core = "1"             # NPU core 1
# cpu_core = 1              # Optional CPU core binding

[speech_tts]
device = "default"         # ALSA playback device
# cpu_core = 2              # Optional CPU core binding

[env_sound_det]
npu_core = "0"             # NPU core 0
score_threshold = 0.3      # Detection threshold

[handpose_det]
npu_core = "2"             # NPU core 2
confidence = 0.5           # Detection confidence

[signlang_det]
npu_core = "0"             # NPU core 0
sequence_length = 30       # 30-frame window
confidence_threshold = 0.6 # Recognition confidence
max_representative_samples = 3
consecutive_hit_windows = 2

[signlang_manager]
bluetooth_name = "SignLang Eyes"
stream_fps = 30
```

## Monitoring and Control

### Graceful Shutdown

Send `SIGINT` or `SIGTERM` to the launcher:
```bash
# From another terminal
pkill -SIGTERM launcher

# Or press Ctrl+C if running in foreground
```

The launcher will:
1. Send `SIGTERM` to all child processes
2. Wait for all children to exit
3. Exit cleanly

### Automatic Restart on Child Failure

The launcher does **not** automatically restart failed children. On any child exit:
1. Logs the exit status
2. Sends `SIGTERM` to all remaining children
3. Exits with non-zero status

For automatic restart, wrap the launcher in a supervisor like `systemd`:

```ini
[Unit]
Description=Sign Language Recognition System
After=network.target

[Service]
Type=simple
User=signlang
WorkingDirectory=/opt/signlang-eyes
ExecStart=/opt/signlang-eyes/launcher
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

## Performance Characteristics

- **Startup time**: ~2-3s (module initialization + RKNN model loading across all modules)
- **Monitor loop overhead**: <0.1% CPU (500ms sleep between checks)
- **Memory**: ~2MB launcher process only (excludes child processes)
- **Child isolation**: Each module runs in separate process with independent address space
- **Shutdown time**: <2s (SIGTERM propagation + graceful child exit)
- **Process tree depth**: 2 levels (launcher → 8 child modules)

## Troubleshooting

### Module Failed to Start

Check the launcher logs for exec failures:
```bash
# Launcher logs to stdout/stderr by default
/opt/signlang-eyes/launcher 2>&1 | tee launcher.log
```

Common causes:
- Missing executable in `bin/` directory
- Missing shared library in `lib/` (check `LD_LIBRARY_PATH`)
- Missing model files in `models/`

### Configuration Parse Error

Syntax error in TOML file:
```bash
# Test TOML syntax
python3 -c "import tomli; tomli.load(open('conf/conf.toml', 'rb'))"
```

### Module Crashes Immediately

Check individual module logs:
```bash
# Module logs are in log/ directory
tail -f log/*.log
```

### IPC Service Conflicts

If previous run didn't clean up:
```bash
# List active iceoryx2 services
iox2-list

# Clean up stale services (if needed)
# Restart launcher - it will recreate services
```

### Permission Denied

Ensure proper permissions:
```bash
# Camera device
sudo usermod -a -G video $USER

# Audio device
sudo usermod -a -G audio $USER

# Log out and log back in for group changes to take effect
```
