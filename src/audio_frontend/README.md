# audio_frontend — Audio Capture & Publishing

## Overview

The **audio_frontend** module captures raw PCM audio from an ALSA audio device (e.g., microphone) and publishes it as `AudioFrame` messages over an iceoryx2 publish-subscribe service. It supports optional single-channel Wiener (spectral subtraction) denoising, channel downmixing, and sample-rate conversion.

- **Executable**: `audio_frontend` (installed under `bin/`)
- **IPC Pattern**: Publish-Subscribe (producer)
- **Input**: ALSA capture device (PCM, 16-bit signed integer)
- **Output**: `signlang::audio_frontend::AudioFrame` on iceoryx2

## File Inventory

| File | Description |
|------|-------------|
| `main.cpp` | Entry point; signal handling (SIGINT/SIGTERM), main loop |
| `program_options.{cpp,hpp}` | CLI argument parsing via cxxopts |
| `alsa_capture_device.{cpp,hpp}` | ALSA PCM device capture wrapper |
| `audio_format.hpp` | `AudioFormat`, `AudioFormatRequest` structs and validation constants |
| `audio_frame.hpp` | `AudioFrame` IPC message definition (shared header) |
| `audio_processor.{cpp,hpp}` | Channel mixing, sample-rate conversion, optional Wiener denoising via FFTW3f |
| `audio_publisher.{cpp,hpp}` | iceoryx2 publish-subscribe publisher wrapper |

## Command-Line Parameters

| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| `--device` / `-d` | *(required)* | — | ALSA audio device name (e.g., `hw:0,0`, `default`) |
| `--service` / `-s` | *(required)* | — | iceoryx2 publish-subscribe service name for audio output |
| `--period-ms` / `-p` | `100` | `1–1000` | Audio publish period in milliseconds |
| `--capture-rate` | (device default) | `8000–192000` | Requested ALSA capture sample rate in Hz |
| `--capture-channels` | (device default) | `1–8` | Requested ALSA capture channel count |
| `--publish-rate` | (matches capture) | `8000–192000` | Published audio sample rate in Hz (≤ capture rate) |
| `--publish-channels` | (matches capture) | `1–8` | Published audio channel count (≤ capture channels) |
| `--denoise` | `false` | — | Enable lightweight Wiener noise reduction (spectral subtraction) |
| `--localization-blackboard` | *(disabled)* | — | iceoryx2 blackboard service name for per-channel sound source proximity |
| `--localization-tdoa-weight` | `0.7` | `0.0–1.0` | TDOA contribution to proximity fusion |
| `--localization-rms-weight` | `0.3` | `0.0–1.0` | RMS energy contribution; must sum with TDOA weight to `1.0` |
| `--help` / `-h` | — | — | Print usage |

## Technical Details

### Audio Processing

- **Format**: 16-bit signed integer PCM, mono by default
- **Sample Rate**: Default 16 kHz; auto-negotiated with ALSA device if not specified
- **Channel Mixing**: If capture and publish channel counts differ, channels are averaged down
- **Publish Window**: Each published frame contains `sample_rate × period_ms / 1000` samples per channel

### Wiener Denoising (`--denoise`)

When enabled, the module applies single-channel spectral subtraction:
- FFTW3f-based STFT (real-to-complex)
- Noise profile estimation from silent frames
- Wiener filter gain applied in frequency domain
- Inverse FFT reconstruction

### Sound Source Channel Proximity

When `--localization-blackboard` is set, the module estimates which captured channel is closest to the active sound
source before any downmixing or resampling. The result is written to a single-entry iceoryx2 blackboard using
`SoundSourceLocalizationKey{.id = 0}`.

- TDOA is estimated from pairwise normalized cross-correlation over the latest capture window.
- RMS energy is used as an auxiliary score when correlation is weak or channels are close.
- `--localization-tdoa-weight` and `--localization-rms-weight` configure fusion and must sum to `1.0`.
- `proximity[ch]` is normalized to sum to `1.0` across active channels.
- `strongest_channel` is the channel with the highest fused proximity score.
- `valid == false` means the frame is too quiet or unusable.

The launcher enables this automatically on the hardcoded `audio_source_localization` blackboard service.

### AudioFrame Metadata

Each published `AudioFrame` carries:
- Sample rate, channel count, bits per sample
- Timestamp (nanoseconds) and sequence number
- Publish period and frame sample count
- Raw PCM sample data in the fixed-size `samples` array

## Dependencies

- **ALSA** (`libasound`): Audio capture
- **FFTW3f**: Single-precision FFT (denoising only)
- **iceoryx2**: Zero-copy IPC publishing

## Usage Example

```bash
# Basic usage — capture from default mic, publish at 16 kHz mono
./audio_frontend \
    --device hw:0,0 \
    --service audio_capture

# With custom format and denoising
./audio_frontend \
    --device hw:0,0 \
    --service audio_capture \
    --capture-rate 48000 \
    --capture-channels 2 \
    --publish-rate 16000 \
    --publish-channels 1 \
    --period-ms 50 \
    --denoise \
    --localization-blackboard audio_source_localization \
    --localization-tdoa-weight 0.7 \
    --localization-rms-weight 0.3

# List available devices
arecord -l
```
