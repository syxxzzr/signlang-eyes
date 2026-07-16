# SignLang Eyes

[![License](https://img.shields.io/badge/license-Apache--2.0-0f766e)](LICENSE)

[简体中文](README.md) | [English](README.en.md)

An on-device, real-time assistive system for the hearing-impaired: dual-hand sign language recognition, speech recognition and synthesis, dangerous sound alerts, display interaction, and location telemetry — all running on the RK3588 platform.

This project also includes an open-source sign language vocabulary management tool that uses `Web Bluetooth` to connect to devices and manage sign language lexicons. For details, see [https://github.com/syxxzzr/signlang-eyes-configurator]()

> This project currently supports `aarch64` / `arm64` / `armv8` only, with the Rockchip RK3588 as the primary target platform. Full operation requires a camera, audio devices, RKNN models, and corresponding peripherals. It is **not** a desktop application that runs on a generic x86_64 machine.

## Overview

SignLang Eyes organizes a camera, microphone, speaker, display, and communication modules into a set of independent processes. Audio/video frames and recognition results are exchanged through iceoryx2 shared-memory IPC. Compute-intensive models leverage the RK3588 NPU whenever possible; image transformation and scaling use the RGA hardware engine; and business logic is coordinated by a central state machine.

The system is built around four usage scenarios:

- **Sign Language Communication** — Detects 21 landmarks on both hands and recognizes complete actions using temporal encoding and two-stage prototype matching.
- **Sign Language AI** — Accumulates consecutive sign language results into a prompt, calls an OpenAI-compatible API, and displays the response on screen.
- **Speech Transcription** — Transcribes Chinese or English speech to text using Whisper and shows the result on an external OLED display.
- **Environmental Awareness** — Detects dangerous sounds with YAMNet and alerts the user through state transitions, vibration, and MQTT notifications.

Beyond these core scenarios, the project also provides device-side capabilities such as BLE gesture library management, serial-port button and display control, GNSS positioning, and MQTT telemetry.

## Key Capabilities

| Domain | Capability | Implementation |
| --- | --- | --- |
| Sign Language Recognition | Dual-hand detection, action segmentation, temporal encoding, dynamically expandable vocabulary | MediaPipe hand model, Temporal Encoder, DTW, SQLite |
| Speech Interaction | Chinese/English speech recognition, Chinese speech synthesis | Whisper, Piper, cpp-pinyin |
| Environmental Safety | Environmental sound classification, danger states, vibration and remote alerts | YAMNet, state machine, MQTT |
| On-Device Inference | Multi-NPU core selection, per-model core assignment | RKNN Runtime |
| Audio/Video Frontend | ALSA capture with sound source localization, V4L2 capture, hardware image processing | ALSA, FFTW3, V4L2, RGA, libjpeg-turbo |
| Device Interaction | OLED text display, serial-port buttons, BLE GATT, GNSS | Serial protocol, BlueZ, GLib/GIO, minmea |
| Inter-Process Communication | Zero-copy data transfer, event notifications, blackboard, and request-response patterns | iceoryx2 |

## System Architecture

The `launcher` reads a TOML configuration file, then starts and supervises 13 child processes in dependency order. If any module fails to start or exits unexpectedly, the launcher stops the entire process group and restarts according to the `restart_attempts` policy.

```text
Microphone
  └─ audio_frontend ─┬─ speech_asr ───────────────┐
                     └─ env_sound_det ──┐         │
                                        ▼         ▼
Camera                             state_machine  dataflow_dispatcher
  └─ video_frontend                  ▲  │         ├─ speech_tts ──> Speaker
       └─ handpose_det ─┬─ signlang_det ──────────┤
                        └─ signlang_manager       ├─ llm_client ──> AI Service
                              ▲                   └─ peripheral_service ──> OLED / Vibration
                              └──── BLE Gesture Management       │
                                                                 ▼
GNSS ───────────────────────────────> telemetry_service ──> MQTT
```

Inter-module service names are fixed by the launcher and cannot be changed via TOML. Audio/video data uses publish-subscribe messaging; state, control, and business logic use events, blackboard, and request-response patterns, respectively.

## Application States

The system starts in `Normal` by default. The base states toggle among `Normal`, `Asr`, `SignLanguageChat`, and `SignLanguageAi`. `DangerousSound` is a special state with a timeout-based recovery.

| State | System Behavior |
| --- | --- |
| `Normal` | Environmental sound detection active; speech and sign language inference idle |
| `Asr` | Whisper speech recognition enabled; transcription displayed on OLED |
| `SignLanguageChat` | Hand and sign language recognition enabled; results spoken via TTS |
| `SignLanguageAi` | Sign language fragments accumulated; OpenAI-compatible API called; reply displayed |
| `DangerousSound` | Danger warning displayed, vibration activated, telemetry alert sent; reverts to the base state after timeout |

## Requirements

### Target Hardware

- Rockchip RK3588 or a compatible aarch64 Linux device
- V4L2-compatible camera
- ALSA-compatible microphone and playback device
- Working RKNN Runtime and RGA driver
- A full deployment also expects a BlueZ BLE adapter, OLED/button peripheral serial port, and a GNSS serial port
- Sign language AI and telemetry features require access to an OpenAI-compatible service or an MQTT broker, respectively

Individual modules can be started in isolation during development, but when running the full system via the launcher, all devices and runtime assets referenced in the configuration must be available — otherwise the failing module will trigger a full process-group restart.

### Build Environment

- CMake 3.20+
- Conan 2.x
- GCC 11+ with C++17
- `aarch64-linux-gnu-gcc`, `aarch64-linux-gnu-g++`
- Target system sysroot; default location is `/root/sysroot`

System libraries include ALSA, OpenSSL, and GLib/GIO. All other major C++ dependencies are managed through Conan.

## Quick Start

For complete instructions on setting up the development environment, configuring Conan, and cross-compiling, see [docs/quick_start.md](docs/quick_start.md). The commands below outline the main workflow; run them from the repository root.

### 1. Prepare Conan

```bash
export CONAN_HOME="$PWD/.conan"                   # optional: isolate Conan cache
export SIGNLANG_AARCH64_SYSROOT=/path/to/sysroot

conan profile detect --force
conan export conan/recipes/cpp-pinyin
conan export conan/recipes/iceoryx2
conan export conan/recipes/librga
conan export conan/recipes/rknn-runtime
```

### 2. Install Dependencies and Cross-Compile

```bash
conan install . \
  -of build-aarch64 \
  -pr:h conan/profiles/linux-aarch64-gcc \
  -pr:b default \
  --build=missing

cmake -S . -B build-aarch64 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=build-aarch64/conan_toolchain.cmake \
  -DCMAKE_INSTALL_PREFIX=install

cmake --build build-aarch64 --target install -j"$(nproc)"
```

For native builds directly on an aarch64 device, prepare dependencies through Conan first, then configure CMake with the generated `conan_toolchain.cmake` as above.

### 3. Prepare Runtime Assets

To avoid committing large or deployment-specific files, `models/`, `*.sqlite`, and `*.hex` are excluded by `.gitignore` and are not included in a clean clone. At minimum, prepare the following according to your configuration:

```text
models/
├── whisper/      Whisper encoder, decoder, vocabulary, and Mel filter
├── piper/        Piper encoder, decoder, and voice configuration
├── yamnet/       YAMNet RKNN model and class labels
├── mediapipe/    Palm detection and hand landmark RKNN models
└── signlang/     Sign language temporal RKNN encoder

conf/
├── conf.toml
├── system_prompt.txt
├── unifont-17.0.05.hex
└── prototypes.sqlite
```

For release images, pass `-DSIGNLANG_RUNTIME_ASSETS_REQUIRED=ON` at CMake configure time to make missing assets a hard error rather than a warning.

Sign language recognition also requires a `prototypes.sqlite` database populated with samples. BLE uploads use the same processing pipeline as live recognition before `signlang_det` writes them to the database; the temporal encoder does not contain a fixed vocabulary.

### 4. Configure and Launch

First, adjust the camera, audio, serial port, NPU core, MQTT, and LLM parameters in [conf/conf.toml](conf/conf.toml) to match your target device, then deploy the install directory to the RK3588 device:

```bash
install/launcher
```

You can also specify an alternative configuration file:

```bash
install/launcher --config /path/to/conf.toml
```

The launcher changes its working directory to the install root, so relative paths such as `conf/`, `models/`, and `log/` are resolved independently of the directory from which the command was invoked.

## Configuration

[conf/conf.toml](conf/conf.toml) lists all supported configuration keys and their default values. Commonly adjusted settings include:

- `[launcher]` — Number of restart attempts after a full process-group failure; `-1` means unlimited retries.
- `[logging]` — Global log level, max file size, and retention count; individual modules can override the log level.
- `[audio_frontend]` / `[video_frontend]` — Device node, sample format, resolution, mirroring, and rotation.
- `[speech_asr]` / `[speech_tts]` — Model paths, language, playback device, and NPU core.
- `[handpose_det]` / `[signlang_det]` — Detection, tracking, temporal window, DTW, and input-quality parameters.
- `[signlang_manager]` — BLE name, adapter, streaming frame rate, and upload limits.
- `[telemetry_service]` — GNSS serial port and MQTT connection parameters.
- `[peripheral_service]` — Serial port, font, OLED dimensions, and scrolling display settings.
- `[llm_client]` — OpenAI-compatible endpoint URL, API key, model name, and timeout.

## Module Overview

| Module                 | Function                                                     |
|------------------------|--------------------------------------------------------------|
| `launcher`             | Reads configuration, launches 13 child processes, monitors and restarts the process group |
| `state_machine`        | Manages base states, the dangerous-sound special state, and state transitions |
| `audio_frontend`       | ALSA audio capture, resampling, mixing, and sound source localization |
| `video_frontend`       | V4L2 capture, MJPEG decoding, and RGA image processing |
| `speech_asr`           | Whisper sliding-window speech recognition |
| `speech_tts`           | Piper Chinese speech synthesis with ALSA playback |
| `env_sound_det`        | YAMNet environmental sound recognition and danger-state triggering |
| `handpose_det`         | Palm detection, dual-hand landmarks, tracking, and smoothing |
| `signlang_det`         | Action segmentation, 168-dimensional features, temporal encoding, two-stage matching, and prototype storage |
| `signlang_manager`     | BLE hand data streaming and gesture vocabulary management endpoint |
| `dataflow_dispatcher`  | Routes ASR, sign language, TTS, LLM, and display data based on the current state |
| `peripheral_service`   | Serial-port buttons, OLED text display, vibration, and alert events |
| `telemetry_service`    | NMEA position parsing and MQTT location/alert reporting |
| `llm_client`           | OpenAI-compatible Chat Completions request service |

Command-line arguments documented in each module's README are suitable for single-module debugging. For day-to-day full-system operation, always use the `launcher` with a TOML configuration.

## Project Structure

```text
signlang-eyes/
├── CMakeLists.txt             Root build configuration and install rules
├── conanfile.txt              Conan dependency manifest
├── conan/
│   ├── profiles/              aarch64 cross-compilation profile
│   └── recipes/               Project-maintained Conan recipes
├── conf/                      TOML configuration and system prompt
├── docs/                      Quick start and other project documentation
├── models/                    Local runtime models (not version-controlled)
└── src/
    ├── common/                Shared runtime utilities, logging, and IPC helpers
    ├── launcher/              Process orchestration
    ├── state_machine/         State management
    ├── audio_frontend/        Audio capture
    ├── video_frontend/        Video capture
    ├── speech_asr/            Speech recognition
    ├── speech_tts/            Speech synthesis
    ├── env_sound_det/         Environmental sound recognition
    ├── handpose_det/          Hand landmark detection
    ├── signlang_det/          Sign language recognition
    ├── signlang_manager/      BLE and gesture library management
    ├── dataflow_dispatcher/   Business data routing
    ├── peripheral_service/    Display and peripheral control
    ├── telemetry_service/     Positioning and telemetry
    └── llm_client/            LLM interface
```

## Acknowledgements

We are grateful to the following open-source projects and their maintainers. The on-device audio/video capture, model inference, inter-process communication, and device interaction capabilities of this project would not be possible without their foundational work:

- **IPC & Async Infrastructure** — [iceoryx2](https://github.com/eclipse-iceoryx/iceoryx2), [Boost](https://www.boost.org/) (JSON, Container, Asio, Beast, MQTT5), [OpenSSL](https://www.openssl.org/)
- **AI Inference & Hardware Acceleration** — [ONNX Runtime](https://onnxruntime.ai/), [RKNN Runtime](https://github.com/airockchip/rknn-toolkit2), [Rockchip RGA](https://github.com/airockchip/librga), [libjpeg-turbo](https://libjpeg-turbo.org/)
- **Audio, Data & Device Support** — [ALSA](https://www.alsa-project.org/), [FFTW](https://www.fftw.org/), [SQLite](https://www.sqlite.org/), [SQLiteCpp](https://github.com/SRombauts/SQLiteCpp), [GLib](https://docs.gtk.org/glib/) / [GIO](https://docs.gtk.org/gio/), [BlueZ](https://www.bluez.org/), [minmea](https://github.com/kosma/minmea)
- **Build & General-Purpose Libraries** — [CMake](https://cmake.org/), [Conan](https://conan.io/), [spdlog](https://github.com/gabime/spdlog), [fmt](https://github.com/fmtlib/fmt), [toml++](https://github.com/marzer/tomlplusplus), [cxxopts](https://github.com/jarro2783/cxxopts), [cpp-pinyin](https://github.com/wolfgitpr/cpp-pinyin)
- **Models & Algorithm Ecosystem** — [OpenAI Whisper](https://github.com/openai/whisper), [Google YAMNet](https://github.com/tensorflow/models/tree/master/research/audioset/yamnet), [Google MediaPipe](https://github.com/google-ai-edge/mediapipe), [Piper](https://github.com/rhasspy/piper)

All third-party libraries, models, and associated data remain subject to their respective licenses, copyrights, and terms of use. We thank all contributors for their continued maintenance of these projects and for making their work available to the community.
