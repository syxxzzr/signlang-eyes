# signlang-eyes

`signlang-eyes` is an RKNN-based edge AI pipeline for sign language interaction and safety alerts on aarch64 devices. It captures audio and video, runs ASR, environmental sound detection, hand pose detection, and sign language recognition, then coordinates module activity through a shared application state machine.

## System Layout

The installed entry point is `launcher`. It reads `conf/conf.toml`, starts all runtime modules, and shuts the whole system down if any child process exits.

```text
audio_capture        -> audio_frontend -> speech_asr, env_sound_det
video_capture        -> video_frontend -> handpose_det -> signlang_det
app_state_event      -> state_machine -> speech_asr, handpose_det, signlang_det
app_state_blackboard -> state_machine -> speech_asr, handpose_det, signlang_det
app_state_control    -> env_sound_det -> state_machine
```

Runtime modules installed under `bin/`:

| Module | Purpose |
|--------|---------|
| `state_machine` | Publishes the global app state and accepts state-control requests |
| `audio_frontend` | Captures ALSA PCM audio, optional denoise/localization, publishes `AudioFrame` |
| `video_frontend` | Captures V4L2 YUYV/MJPEG frames and publishes video byte slices |
| `speech_asr` | Runs Whisper ASR when the app state is `Asr` |
| `env_sound_det` | Runs YAMNet and requests `DangerousSound` for configured horn labels |
| `handpose_det` | Runs YOLOv8 hand pose when sign-language states are active |
| `signlang_det` | Runs BiLSTM + DTW sign language recognition from hand pose windows |

## Application States

The default state is `Normal`. In that state, ASR and sign-language inference modules remain disabled. ASR runs only in `Asr`; hand pose and sign language recognition run in `SignLanguageChat` and `SignLanguageAi`. `DangerousSound` is a special alert state requested by environmental sound detection and auto-expires back to the current base state.

## Repository Structure

```text
conf/                 Default launcher TOML configuration
models/               RKNN models and label maps used by runtime modules
src/audio_frontend/   ALSA capture and audio IPC publisher
src/video_frontend/   V4L2 capture and video IPC publisher
src/speech_asr/       Whisper ASR module
src/env_sound_det/    YAMNet environmental sound detector
src/handpose_det/     YOLOv8 hand pose detector
src/signlang_det/     Sign language recognizer
src/state_machine/    Global app state controller
src/launcher/         Process supervisor and launcher
third_party/          Vendored headers and target runtime libraries
```

Each module has its own `README.md` under `src/<module>/` with command-line options and IPC details.

## Build

This project targets aarch64/arm64 only. Configure on the target device or with an aarch64 toolchain:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --install build --prefix install
```

The root CMake project expects the vendored dependencies under `third_party/`:

- iceoryx2 C++ libraries
- RKNN runtime headers and `librknnrt.so`
- FFTW3f for ASR/audio processing
- cxxopts and toml++ headers
- ALSA and pthreads from the target system

## Run

After installation:

```bash
cd install
./launcher
```

Use a custom configuration file:

```bash
./launcher --config /path/to/conf.toml
```

The launcher hardcodes IPC service names and ignores IPC service-name keys in TOML. Configure device paths, model paths, NPU core selection, thresholds, and timing options in `conf/conf.toml`.

## Install Layout

```text
install/
|-- launcher
|-- bin/
|   |-- state_machine
|   |-- audio_frontend
|   |-- video_frontend
|   |-- speech_asr
|   |-- env_sound_det
|   |-- handpose_det
|   `-- signlang_det
|-- conf/
|   `-- conf.toml
|-- lib/
|   |-- libiceoryx2_cxx.so
|   |-- libiceoryx2_ffi_c.so
|   `-- librknnrt.so
`-- models/
    |-- whisper/
    |-- yamnet/
    |-- yolov8n-handpose/
    `-- signlang/
```
