# audio_frontend — Audio Capture & Publishing

## Overview

The **audio_frontend** module captures raw PCM audio from an ALSA audio device (e.g., microphone) and publishes it as `AudioFrame` messages over an iceoryx2 publish-subscribe service. It supports channel downmixing, sample-rate conversion, and optional sound source localization.

- **Executable**: `audio_frontend` (installed under `bin/`)
- **IPC Pattern**: Publish-Subscribe (producer)
- **Input**: ALSA capture device (PCM, 16-bit signed integer)
- **Output**: `signlang::audio_frontend::AudioFrame` on iceoryx2

## Command-Line Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--device` / `-d` | *(required)* | ALSA audio device name (e.g., `hw:0,0`, `default`) |
| `--service` / `-s` | *(required)* | iceoryx2 publish-subscribe service name for audio output |
| `--period-ms` / `-p` | `100` | Audio publish period in milliseconds (must be > 0) |
| `--capture-rate` | (device default) | Requested ALSA capture sample rate in Hz (must be > 0) |
| `--capture-channels` | (device default) | Requested ALSA capture channel count (must be > 0) |
| `--publish-rate` | (matches capture) | Published audio sample rate in Hz (must be > 0, ≤ capture rate) |
| `--publish-channels` | (matches capture) | Published audio channel count (must be > 0, ≤ capture channels) |
| `--localization-blackboard` | *(disabled)* | iceoryx2 blackboard service name for per-channel sound source proximity |
| `--localization-tdoa-weight` | `0.7` | TDOA contribution to proximity fusion (0.0-1.0) |
| `--localization-rms-weight` | `0.3` | RMS energy contribution; must sum with TDOA weight to 1.0 |
| `--help` / `-h` | — | Print usage |

## Technical Details

### Audio Processing

- **Format**: 16-bit signed integer PCM
- **Sample Rate**: Default 16 kHz; auto-negotiated with ALSA device if not specified
- **Channel Mixing**: If capture and publish channel counts differ, channels are averaged down
- **Publish Window**: Each published frame contains `sample_rate × period_ms / 1000` samples per channel

### Sound Source Channel Proximity

When `--localization-blackboard` is set, the module estimates which captured channel is closest to the active sound source before any downmixing or resampling. The result is written to a single-entry iceoryx2 blackboard using `SoundSourceLocalizationKey{.id = 0}`.

- TDOA (Time Difference of Arrival) is estimated from pairwise normalized cross-correlation over the latest capture window
- RMS energy is used as an auxiliary score when correlation is weak or channels are close
- `--localization-tdoa-weight` and `--localization-rms-weight` configure fusion and must sum to 1.0
- `proximity[ch]` is normalized to sum to 1.0 across active channels
- `strongest_channel` is the channel with the highest fused proximity score
- `valid == false` means the frame is too quiet or unusable

The launcher enables this automatically on the hardcoded `audio_source_localization` blackboard service.

### AudioFrame Metadata

Each published `AudioFrame` carries:
- Sample rate, channel count, bits per sample
- Timestamp (nanoseconds) and sequence number
- Publish period and frame sample count
- Raw PCM sample data in the fixed-size `samples` array

## Architecture

```
ALSA Device (hw:2,0)
        │
        ▼
AlsaCaptureDevice
   (16-bit PCM)
        │
        ▼
AudioProcessor
(mixing, resampling)
        │
        ├─────────────────────────┐
        ▼                         ▼
SoundSourceLocalization    AudioPublisher
(optional blackboard)      (iceoryx2 pub)
```

## Dependencies

- **ALSA** (`libasound`): Audio capture
- **iceoryx2**: Zero-copy IPC publishing
- **FFTW3f**: FFT for cross-correlation in sound source localization

## Usage Examples

### Basic Usage

```bash
# Capture from default mic, publish at 16 kHz mono
./audio_frontend \
    --device hw:0,0 \
    --service audio_capture
```

### Custom Format

```bash
# Stereo capture, mono publish with resampling
./audio_frontend \
    --device hw:0,0 \
    --service audio_capture \
    --capture-rate 48000 \
    --capture-channels 2 \
    --publish-rate 16000 \
    --publish-channels 1 \
    --period-ms 50
```

### With Sound Source Localization

```bash
# Enable TDOA-based source localization
./audio_frontend \
    --device hw:0,0 \
    --service audio_capture \
    --capture-rate 16000 \
    --capture-channels 2 \
    --publish-rate 16000 \
    --publish-channels 1 \
    --localization-blackboard audio_source_localization \
    --localization-tdoa-weight 0.7 \
    --localization-rms-weight 0.3
```

### List Available ALSA Devices

```bash
arecord -l
```

## File Organization

| File | Description |
|------|-------------|
| `main.cpp` | Entry point; signal handling (SIGINT/SIGTERM), main loop |
| `program_options.{cpp,hpp}` | CLI argument parsing via cxxopts |
| `alsa_capture_device.{cpp,hpp}` | ALSA PCM device capture wrapper |
| `audio_format.hpp` | `AudioFormat`, `AudioFormatRequest` structs |
| `audio_frame.hpp` | `AudioFrame` IPC message definition |
| `audio_processor.{cpp,hpp}` | Channel mixing and sample-rate conversion |
| `audio_publisher.{cpp,hpp}` | iceoryx2 publish-subscribe publisher wrapper |
| `sound_source_localization.{cpp,hpp}` | TDOA and RMS-based source proximity estimation |
| `sound_source_blackboard.{cpp,hpp}` | iceoryx2 blackboard writer for localization results |

## IPC Data Structures

### AudioFrame (Published)

```cpp
struct AudioFrame {
    std::uint32_t sample_rate_hz;
    std::uint32_t channel_count;
    std::uint32_t bits_per_sample;
    std::uint64_t timestamp_ns;
    std::uint64_t sequence_number;
    std::uint32_t period_ms;
    std::uint32_t frame_sample_count;
    std::array<std::int16_t, kMaxAudioSamples> samples;
};
```

### SoundSourceLocalization (Blackboard)

```cpp
struct SoundSourceLocalization {
    bool valid;
    std::uint32_t strongest_channel;
    std::array<float, kMaxAudioChannels> proximity;
};
```

## Performance Characteristics

- **Zero-copy publishing**: Audio samples published directly from capture buffer when no processing is needed
- **Event-driven**: No polling loops, responds immediately to ALSA buffer availability
- **Low latency**: Typical glass-to-glass latency < 100ms at 50ms period
- **CPU usage**: ~5-10% on single core during active capture (Cortex-A76)
