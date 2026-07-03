# signlang-eyes

Real-time sign language recognition and environmental awareness system for the hearing-impaired, providing dual-hand sign language translation, speech recognition, and hazardous sound detection with NPU-accelerated inference.

## System Architecture

Modular architecture using iceoryx2 zero-copy IPC for high-performance inter-process communication:

```
audio_frontend ─┬─▶ speech_asr ───────────────┐
                └─▶ env_sound_det ─▶ state_machine

video_frontend ─▶ handpose_det ─┬─▶ signlang_det ─┐
                                └─▶ signlang_manager

state_machine ─▶ dataflow_dispatcher
speech_asr ─────▶ dataflow_dispatcher ─▶ peripheral_service display
signlang_det ───▶ dataflow_dispatcher ─┬─▶ speech_tts
                                       ├─▶ llm_client
                                       └─▶ peripheral_service display

peripheral_service ─▶ state_machine
position_service ───▶ peripheral_service
```

### Core Modules

- **audio_frontend**: ALSA audio capture with TDOA-based sound source localization and adaptive resampling
- **video_frontend**: V4L2 camera capture with RGA hardware-accelerated format conversion (YUYV/MJPEG→RGB24) and scaling
- **speech_asr**: Whisper base encoder-decoder speech recognition with 15s sliding window for Chinese/English
- **speech_tts**: Piper/cpp-pinyin Chinese text-to-speech playback from iceoryx2 request-response requests
- **env_sound_det**: YAMNet (MobileNetV1) environmental sound classification with threshold-based dangerous sound alerts
- **handpose_det**: MediaPipe dual-model pipeline (palm detector + hand landmarks) with single-hand or dual-hand output
- **signlang_det**: Dual-hand sign language recognition using BiLSTM encoder + DTW matching against SQLite prototype database
- **signlang_manager**: BLE GATT access point for handpose streaming and runtime gesture prototype database management
- **state_machine**: Global state coordinator managing module lifecycle via Event + Blackboard + Request-Response IPC
- **dataflow_dispatcher**: State-aware routing from ASR/sign language results to display, TTS, and LLM services
- **peripheral_service**: Serial-button input, OLED display rendering, alert notification, and state-control integration
- **position_service**: GNSS/position alert ingestion and forwarding to peripheral alert events
- **llm_client**: OpenAI-compatible request-response client for SignLanguageAi prompts
- **launcher**: Unified process orchestrator loading all modules from TOML configuration with health monitoring

## Application States

Default state is `Normal`. ASR and sign language modules remain disabled in this state:
- **Normal**: Base state, only environmental sound detection active
- **Asr**: Speech recognition enabled
- **SignLanguageChat**: Sign language chat mode
- **SignLanguageAi**: Sign language AI interaction mode
- **DangerousSound**: Hazardous sound alert (auto-expires back to base state)

## Technical Features

- **Zero-copy IPC**: iceoryx2 shared memory eliminates serialization overhead for audio/video streams
- **Event-driven architecture**: `Node::wait()` replaces polling for zero CPU usage when idle
- **NPU acceleration**: RKNN inference engine with RK3588 multi-core parallelism and configurable core affinity
- **Hardware acceleration**: RGA (Raster Graphic Acceleration) unit provides ~50× speedup for YUYV→RGB24 conversion
- **Sliding window processing**: Overlapping temporal analysis for audio (15s/3s) and video (30 frames) streams
- **Threshold-based detection**: env_sound_det uses flexible score filtering (0-32 classes) instead of fixed top-K
- **BiLSTM + DTW hybrid**: Sign language recognition combines neural encoding with dynamic time warping for speed invariance
- **SQLite prototype storage**: Runtime vocabulary boundary decoupled from BiLSTM encoder model
- **BLE management API**: Runtime handpose streaming and gesture add/list/delete over a custom GATT service

## Build

### Dependencies

- CMake 3.20+
- GCC 11+ with C++17 support
- Target platform: aarch64/arm64 (RK3588 or compatible)
- iceoryx2 (zero-copy IPC framework)
- RKNN Runtime 2.0+ (Rockchip NPU inference)
- ALSA (libasound, audio capture)
- V4L2 (Video4Linux2, camera capture)
- spdlog 1.17+ (logging with rotation)
- FFTW3f (FFT for audio spectrogram and cross-correlation)
- libjpeg-turbo (MJPEG decode, provided by Conan and linked statically)
- librga 2.0+ (Rockchip RGA hardware acceleration)
- SQLiteCpp (sign language prototype database)
- Boost JSON/Container and minmea (LLM JSON handling and GNSS parsing)
- cxxopts, toml++ (CLI and config parsing, header-only)
- GLib/GIO + BlueZ runtime (BLE GATT service for signlang_manager)

### Cross-compilation

Requires a system cross-compilation toolchain targeting aarch64, such as `aarch64-linux-gnu-gcc` and
`aarch64-linux-gnu-g++` on `PATH`. The Conan profile defaults to `/home/sysroot`; override it with
`SIGNLANG_AARCH64_SYSROOT` when needed.

```bash
SIGNLANG_AARCH64_SYSROOT=/home/sysroot \
conan install . -pr:h conan/profiles/linux-aarch64-gcc -pr:b default \
      -of build-aarch64 --build=missing

cmake -S . -B build-aarch64 \
      -DCMAKE_TOOLCHAIN_FILE=build-aarch64/conan_toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build-aarch64 -j$(nproc)
cmake --install build-aarch64 --prefix install
```

### Native build (on aarch64 device)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
cmake --install build --prefix install
```

The installed binaries resolve relative runtime paths from the installation root, not from the shell's current working
directory. `launcher` is installed at the root, and module executables installed under `bin/` automatically use the
parent directory as their runtime root. Defaults such as `conf/conf.toml`, `models/...`, and `log/...` therefore work
when launching from any directory.

### Installation Directory Layout

```
install/
├── launcher          # Main launcher
├── bin/              # Module executables
│   ├── state_machine
│   ├── audio_frontend
│   ├── video_frontend
│   ├── speech_asr
│   ├── speech_tts
│   ├── env_sound_det
│   ├── handpose_det
│   ├── signlang_manager
│   ├── signlang_det
│   ├── dataflow_dispatcher
│   ├── peripheral_service
│   ├── position_service
│   └── llm_client
├── lib/              # Shared libraries
│   ├── libiceoryx2_cxx.so
│   ├── libiceoryx2_ffi_c.so
│   ├── librknnrt.so
│   ├── libspdlog.so
│   └── librga.so
├── log/              # Runtime logs, created on first launcher run
├── conf/             # Configuration files
│   ├── conf.toml
│   └── prototypes.sqlite
└── models/           # AI model files
    ├── whisper/
    ├── yamnet/
    ├── mediapipe/
    ├── bilstm/
    └── piper/
```

## Configuration

Main configuration file `conf/conf.toml` (all keys optional, fallback to module defaults):

```toml
[logging]
rotate_size = 1048576      # Log file size (1MB)
retain_files = 100         # Number of retained log files

[state_machine]
initial_state = "normal"   # normal, asr, sign_language_chat, sign_language_ai

[audio_frontend]
device = "hw:2,0"          # ALSA device name
capture_rate = 16000       # Sample rate in Hz
capture_channels = 2       # Channel count

[video_frontend]
device = "/dev/video21"    # V4L2 device path
output_width = 640         # Output width in pixels
output_height = 480        # Output height in pixels
# mirror_output = false    # Horizontally mirror published video

[speech_asr]
language = "zh"            # Recognition language: "zh" or "en"
npu_core = "1"             # NPU core: "0", "1", "2", "0_1", "0_1_2", "auto", "all"

[speech_tts]
device = "default"         # ALSA playback device

[env_sound_det]
npu_core = "0"
score_threshold = 0.3      # Detection threshold (0.0-1.0)

[handpose_det]
npu_core = "2"
confidence = 0.5           # Detection confidence threshold (0.0-1.0)
single_hand = false        # true for one hand, false for two hands

[signlang_det]
npu_core = "0"
model = "models/bilstm/biltsm.rknn"
prototypes = "conf/prototypes.sqlite"
sequence_length = 30       # Sliding window frame count
confidence_threshold = 0.6 # Recognition confidence threshold (0.0-1.0)
upload_window_overlap = 0.5
max_representative_samples = 3
consecutive_hit_windows = 2

[signlang_manager]
bluetooth_name = "SignLang Eyes"
stream_fps = 30
```

IPC service names are **hardcoded** in the launcher and cannot be configured via TOML.

Relative paths in the TOML are resolved from the installation root when launched through `launcher`.

## Running

### Launch all modules

```bash
install/launcher --config conf/conf.toml
```

Or use default configuration:

```bash
install/launcher
```

The launcher spawns all modules in dependency order, monitors their health, and performs coordinated shutdown on SIGINT/SIGTERM or child failure.

### Run individual modules

```bash
# State machine
install/bin/state_machine \
    --state-event-service app_state_event \
    --state-blackboard-service app_state_blackboard \
    --state-control-service app_state_control \
    --initial-state normal

# Audio capture
install/bin/audio_frontend \
    --device hw:2,0 \
    --service audio_capture \
    --capture-rate 16000

# Video capture
install/bin/video_frontend \
    --device /dev/video21 \
    --service video_capture \
    --output-width 640 \
    --output-height 480

# Speech recognition
install/bin/speech_asr \
    --input-service audio_capture \
    --output-service speech_asr_result \
    --language zh \
    --npu-core 1

# State-aware routing
install/bin/dataflow_dispatcher \
    --state-event-service app_state_event \
    --state-blackboard-service app_state_blackboard \
    --signlang-result-service signlang_result \
    --speech-asr-result-service speech_asr_result \
    --speech-tts-service speech_tts \
    --llm-client-service llm_client \
    --peripheral-display-service peripheral_display

# Environmental sound detection
install/bin/env_sound_det \
    --input-service audio_capture \
    --state-control-service app_state_control \
    --npu-core 0 \
    --score-threshold 0.3

# Hand pose detection
install/bin/handpose_det \
    --input-service video_capture \
    --output-service handpose_result \
    --npu-core 2 \
    --confidence 0.5

# BLE sign language manager
install/bin/signlang_manager \
    --input-service handpose_result \
    --signlang-result-service signlang_result \
    --gesture-management-service signlang_gesture_management

# Sign language recognition
install/bin/signlang_det \
    --input-service handpose_result \
    --output-service signlang_result \
    --prototype-control-service signlang_prototype_control \
    --gesture-management-service signlang_gesture_management \
    --npu-core 0 \
    --sequence-length 30

# Text-to-speech playback
install/bin/speech_tts \
    --service speech_tts \
    --device default

# Peripheral display/buttons
install/bin/peripheral_service \
    --display-service peripheral_display \
    --state-control-service app_state_control \
    --alert-event-service position_alert

# LLM request service
install/bin/llm_client \
    --service llm_client
```

## Logging

When launched through `launcher`, logs are automatically saved to the installation root's `log/` directory:
- Single file size: 1MB (configurable)
- Retained file count: 100 (configurable)
- Format: `[timestamp] [level] [thread] message`
- Auto-rotation prevents disk overflow

## Repository Structure

```
conf/                 Default configuration files
models/               RKNN models and label maps
src/
├── common/           Shared utility libraries
├── audio_frontend/   Audio capture module
├── video_frontend/   Video capture module
├── speech_asr/       Speech recognition module
├── speech_tts/       Piper text-to-speech playback module
├── env_sound_det/    Environmental sound detection module
├── handpose_det/     Hand pose detection module
├── signlang_det/     Sign language recognition module
├── signlang_manager/ BLE handpose stream and gesture DB manager
├── dataflow_dispatcher/ State-aware ASR/sign language result routing
├── peripheral_service/ OLED display, serial buttons, and alert output
├── position_service/  GNSS/position alert listener
├── llm_client/        OpenAI-compatible LLM request service
├── state_machine/    State machine module
└── launcher/         Launcher module
third_party/          Third-party dependencies
cmake/                CMake toolchains and modules
```

## Module Documentation

Detailed documentation for each module:
- [audio_frontend](src/audio_frontend/README.md) - Audio capture and source localization
- [video_frontend](src/video_frontend/README.md) - Video capture and hardware acceleration
- [speech_asr](src/speech_asr/README.md) - Whisper speech recognition
- [speech_tts](src/speech_tts/README.md) - Piper text-to-speech playback
- [env_sound_det](src/env_sound_det/README.md) - YAMNet environmental sound detection
- [handpose_det](src/handpose_det/README.md) - MediaPipe hand pose detection
- [signlang_det](src/signlang_det/README.md) - DTW sign language recognition
- [state_machine](src/state_machine/README.md) - Global state management
- [launcher](src/launcher/README.md) - Process launch and monitoring
- [dataflow_dispatcher](src/dataflow_dispatcher) - State-aware result routing
- [peripheral_service](src/peripheral_service) - OLED display and button peripheral service
- [position_service](src/position_service) - Position alert listener
- [llm_client](src/llm_client) - LLM request service

## IPC Services

Inter-module communication via iceoryx2 services (hardcoded names):
- `audio_capture`: Audio frame data stream (PCM 16-bit, publish-subscribe)
- `video_capture`: Video frame data stream (RGB24, publish-subscribe with user header)
- `handpose_result`: Hand keypoint data stream (21 landmarks for one or two hands, publish-subscribe with user header)
- `speech_asr_result`: Speech recognition transcription results consumed by dataflow_dispatcher in `Asr` state (publish-subscribe)
- `speech_tts`: Text-to-speech requests (request-response)
- `signlang_result`: Sign language recognition results (publish-subscribe)
- `signlang_prototype_control`: Prototype reload/status control between signlang_manager and signlang_det (request-response)
- `signlang_gesture_management`: Gesture database management between signlang_manager and signlang_det (request-response)
- `llm_client`: SignLanguageAi prompt request service (request-response)
- `peripheral_display`: OLED title/content display updates (request-response)
- `position_alert`: Position alert event notifications (event notifier)
- `app_state_event`: State change event notifications (event notifier)
- `app_state_blackboard`: Current application state storage (blackboard, single-entry)
- `app_state_control`: State control requests (request-response)
- `audio_source_localization`: Sound source channel proximity scores (blackboard, single-entry)

## License

See LICENSE file.
