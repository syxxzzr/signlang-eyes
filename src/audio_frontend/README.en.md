# Audio Frontend

[简体中文](README.md) | [English](README.en.md)

ALSA audio capture and publishing module. Captures raw PCM audio from a microphone, performs resampling and channel mixing, publishes audio frames over iceoryx2 publish-subscribe, and optionally performs sound source localization.

## Features

- Captures raw audio data through the ALSA PCM interface
- Converts the capture format (sample rate, channel count) to a unified publish format (default: 16 kHz mono)
- Publishes audio frames over iceoryx2 publish-subscribe for downstream consumers (`speech_asr`, `env_sound_det`)
- Optional: TDOA + RMS energy fusion based sound source localization, results published to an iceoryx2 blackboard
- Automatically pauses capture when no downstream subscribers are connected, conserving system resources

## Processing Pipeline

```text
ALSA device → Capture → AudioProcessor (resample/mix) → AudioPublisher → iceoryx2
                              ↓
                   SoundSourceLocalizer (TDOA + RMS) → iceoryx2 Blackboard
```

## CLI Arguments

| Argument | Description |
| --- | --- |
| `-d, --device <name>` | ALSA device name (required) |
| `-s, --service <name>` | iceoryx2 publish-subscribe service name (required) |
| `--localization-blackboard <name>` | Sound source localization blackboard service name |
| `--localization-tdoa-weight <float>` | TDOA weight (default: 0.5) |
| `--localization-rms-weight <float>` | RMS energy weight (default: 0.5) |
| `-p, --period-ms <ms>` | Audio publish period (default: 10 ms) |
| `--capture-rate <Hz>` | Capture sample rate |
| `--capture-channels <n>` | Capture channel count |
| `--publish-rate <Hz>` | Publish sample rate (default: 16000) |
| `--publish-channels <n>` | Publish channel count (default: 1) |
| `--cpu-core <n>` | CPU core to pin to |

## Sound Source Localization

The localizer fuses time-difference-of-arrival (TDOA) and RMS energy to compute a normalized proximity score (0.0–1.0). The TDOA and RMS weights must sum to 1.0.

## Dependencies

- ALSA (`libasound`)
- iceoryx2 (publish-subscribe, blackboard)
- FFTW3 (frequency-domain analysis for sound source localization)
