# speech_tts - Piper Text-to-Speech Playback

The **speech_tts** module receives text over an iceoryx2 request-response service, accepts the request immediately, and plays the newest accepted text through cpp-pinyin, Piper model assets, ONNX Runtime, RKNN Runtime, and ALSA.

- **Executable**: `speech_tts` (installed under `bin/`)
- **IPC Pattern**: Request-Response server
- **Input**: `signlang::speech_tts::SpeechTtsRequest`
- **Output**: ALSA playback
- **Interruption**: New requests cancel any currently synthesized or playing text.

## Options

| Option | Default | Description |
| --- | --- | --- |
| `--service` / `-s` | *(required)* | iceoryx2 request-response service name |
| `--device` / `-d` | `default` | ALSA playback device |
| `--encoder-model` | `models/piper/zh_CN-huayan-x_low.encoder.onnx` | Piper encoder ONNX model path |
| `--decoder-model` | `models/piper/zh_CN-huayan-x_low.decoder.rknn` | Piper decoder RKNN model path |
| `--config` | `models/piper/zh_CN-huayan-x_low.json` | Piper voice config path |
| `--pinyin-dict` | build-time cpp-pinyin dictionary path | cpp-pinyin dictionary directory |
| `--npu-core` | `auto` | RKNN decoder NPU core mask |
| `--rknn-priority` | `medium` | RKNN decoder priority |
## Request Semantics

The response reports only whether the request was accepted into the playback worker. It does not wait for speech generation or playback to finish.

When a new non-empty text request arrives:

1. The module increments a cancellation generation.
2. The previous pending task is replaced.
3. The response is returned immediately with `SpeechTtsStatus::Ok`.
4. The playback worker stops the previous generation at the next synthesis/playback cancellation check and starts the newest text.
