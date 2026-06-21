# signlang-eyes

Sign language recognition and environmental awareness system for the hearing-impaired, providing real-time sign language translation, speech recognition, and hazardous sound detection.

## System Architecture

Modular architecture using iceoryx2 zero-copy IPC for high-performance inter-process communication:

```
┌─────────────────┐     ┌──────────────────┐
│ audio_frontend  │────▶│   speech_asr     │
│  (ALSA capture) │     │  (Whisper ASR)   │
└─────────────────┘     └──────────────────┘
        │                        │
        │                        ▼
        │               ┌─────────────────┐
        ▼               │ state_machine   │
┌──────────────────┐    │  (State coord)  │
│ env_sound_det    │───▶└─────────────────┘
│ (YAMNet audio)   │             ▲
└──────────────────┘             │
                                 │
┌──────────────────┐             │
│ video_frontend   │─────────────┤
│ (V4L2 camera)    │             │
└──────────────────┘             │
        │                        │
        ▼                        │
┌──────────────────┐             │
│  handpose_det    │─────────────┤
│  (YOLOv8 hand)   │             │
└──────────────────┘             │
        │                        │
        ▼                        │
┌──────────────────┐             │
│  signlang_det    │─────────────┘
│  (DTW matching)  │
└──────────────────┘
```

### Core Modules

- **audio_frontend**: ALSA audio capture with multi-channel source localization and adaptive resampling
- **video_frontend**: V4L2 camera capture with RGA hardware-accelerated YUYV→RGB24 conversion and scaling
- **speech_asr**: Whisper speech recognition with sliding window for Chinese/English
- **env_sound_det**: YAMNet environmental sound detection with threshold-based hazardous sound alerts
- **handpose_det**: YOLOv8 hand keypoint detection, tracking up to 2 hands
- **signlang_det**: Sign language recognition using DTW temporal matching
- **state_machine**: Global state coordinator managing module interactions
- **launcher**: Unified launcher loading all modules from TOML configuration

## Application States

Default state is `Normal`. ASR and sign language modules remain disabled in this state:
- **Normal**: Base state, only environmental sound detection active
- **Asr**: Speech recognition enabled
- **SignLanguageChat**: Sign language chat mode
- **SignLanguageAi**: Sign language AI interaction mode
- **DangerousSound**: Hazardous sound alert (auto-expires back to base state)

## Technical Features

- **Zero-copy IPC**: iceoryx2 shared memory with no serialization overhead
- **Event-driven**: Node::wait() replaces polling for zero CPU usage when idle
- **NPU acceleration**: RKNN inference engine with multi-core parallelism
- **Hardware acceleration**: RGA hardware-accelerated video format conversion and scaling
- **Sliding windows**: Audio and video temporal analysis with overlapping windows
- **Threshold filtering**: env_sound_det supports flexible threshold filtering, detecting up to 32 classes

## Build

### Dependencies

- CMake 3.20+
- C++20 compiler (aarch64/arm64 target platform)
- iceoryx2 (zero-copy IPC)
- RKNN Runtime (Rockchip NPU)
- ALSA (audio)
- V4L2 (video)
- spdlog (logging)
- FFTW3f (audio FFT)
- libjpeg-turbo (JPEG encoding)
- librga (RGA hardware acceleration)
- cxxopts, toml++ (CLI and config parsing)

### Cross-compilation

```bash
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchains/aarch64-buildroot.cmake \
      -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
make install DESTDIR=../install
```

### Native build (aarch64 device)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --install build --prefix install
```

### Installation Directory Layout

```
install/
├── launcher          # Main launcher
├── bin/              # Module executables
│   ├── state_machine
│   ├── audio_frontend
│   ├── video_frontend
│   ├── speech_asr
│   ├── env_sound_det
│   ├── handpose_det
│   └── signlang_det
├── lib/              # Shared libraries
│   ├── libiceoryx2_cxx.so
│   ├── libiceoryx2_ffi_c.so
│   ├── librknnrt.so
│   ├── libspdlog.so
│   └── librga.so
├── conf/             # Configuration files
│   └── conf.toml
└── models/           # AI model files
    ├── whisper/
    ├── yamnet/
    ├── yolov8n-handpose/
    └── signlang/
```

## Configuration

Main configuration file `conf/conf.toml`:

```toml
[logging]
rotate_size = 1048576      # Log file size (1MB)
retain_files = 100         # Number of retained files

[audio_frontend]
device = "hw:2,0"          # ALSA device name
capture_rate = 16000       # Sample rate in Hz
capture_channels = 2       # Channel count

[video_frontend]
device = "/dev/video21"    # V4L2 device path
output_width = 640         # Output width
output_height = 480        # Output height

[speech_asr]
language = "zh"            # Recognition language: "zh" or "en"
npu_core = "1"             # NPU core: "0", "1", "2", "0_1", "0_1_2"

[env_sound_det]
npu_core = "0"
score_threshold = 0.3      # Detection threshold (0.0-1.0)

[handpose_det]
npu_core = "2"
confidence = 0.5           # Detection confidence threshold

[signlang_det]
npu_core = "0"
sequence_length = 30       # Sliding window frame count
confidence_threshold = 0.6 # Recognition confidence threshold
```

## Running

### Launch all modules

```bash
cd install
./launcher --config conf/conf.toml
```

Or use default configuration:

```bash
./launcher
```

### Run individual modules

```bash
# State machine
./bin/state_machine

# Audio capture
./bin/audio_frontend --device hw:2,0 --capture-rate 16000

# Video capture
./bin/video_frontend --device /dev/video21 --width 640 --height 480

# Speech recognition
./bin/speech_asr --language zh --npu-core 1

# Environmental sound detection
./bin/env_sound_det --npu-core 0 --score-threshold 0.3

# Hand pose detection
./bin/handpose_det --npu-core 2 --confidence 0.5

# Sign language recognition
./bin/signlang_det --npu-core 0 --sequence-length 30
```

## Logging

Logs are automatically saved to the `logs/` directory:
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
├── env_sound_det/    Environmental sound detection module
├── handpose_det/     Hand pose detection module
├── signlang_det/     Sign language recognition module
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
- [env_sound_det](src/env_sound_det/README.md) - YAMNet environmental sound detection
- [handpose_det](src/handpose_det/README.md) - YOLOv8 hand keypoint detection
- [signlang_det](src/signlang_det/README.md) - DTW sign language recognition
- [state_machine](src/state_machine/README.md) - Global state management
- [launcher](src/launcher/README.md) - Process launch and monitoring

## IPC Services

Inter-module communication via iceoryx2 services:
- `audio_capture`: Audio frame data stream
- `video_capture`: Video frame data stream
- `handpose_data`: Hand keypoint data stream
- `app_state_event`: State change events
- `app_state_blackboard`: State query service
- `app_state_control`: State control requests

## License

See LICENSE file.
