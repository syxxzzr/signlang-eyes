# Peripheral Service

[简体中文](README.md) | [English](README.en.md)

Peripheral control and service module. Drives the OLED display, vibration motor, and buttons over a serial port, and exposes an iceoryx2 display interface for other modules to update screen content.

## Features

- Serial communication: communicates with the peripheral controller over UART, with configurable baud rate, data bits, stop bits, parity, and flow control
- OLED display: renders text using GNU Unifont bitmap font, supports dual-line display (title + content), with hardware scrolling on the content line
- Button interaction: single-click cycles the application state, double-click triggers an alert event
- Vibration control: activates the vibration motor in `DangerousSound` state
- Exposes a display interface via iceoryx2 request-response, accepting SetTitleLine, SetContentLine, and ClearContentLine commands
- Subscribes to iceoryx2 state events and blackboard to stay synchronized with the current application state

## Display Architecture

```text
IpcDisplayServer (iceoryx2) → DisplayWorker (periodic refresh)
                                  ↓
                            ScrollingDisplay (dual-line management)
                                  ↓
                            TextRenderer + HexFont (Unifont rendering)
                                  ↓
                            SerialTransport (UART protocol)
                                  ↓
                            OLED peripheral
```

## CLI Arguments

| Argument | Description |
| --- | --- |
| `-d, --device <path>` | Serial device path (default: `/dev/ttyS3`) |
| `--baud-rate <rate>` | Serial baud rate (default: 115200) |
| `--data-bits <5-8>` | Data bits (default: 8) |
| `--stop-bits <1|2>` | Stop bits (default: 1) |
| `--parity <none|odd|even>` | Parity mode (default: `none`) |
| `--flow-control <none|software|hardware>` | Flow control mode (default: `none`) |
| `--font-file <path>` | GNU Unifont .hex file path |
| `--display-width <px>` | Display width in pixels (default: 128) |
| `--display-height <px>` | Display height in pixels (default: 32, must be a multiple of 8) |
| `--font-size <px>` | Font size in pixels (default: 16) |
| `--char-spacing <px>` | Inter-character spacing (default: 1) |
| `--line-gap <px>` | Inter-line gap (default: 0) |
| `--scroll-speed-px-per-sec <rate>` | Scroll speed (default: 13 px/s) |
| `--scroll-pause-ms <ms>` | Scroll pause duration (default: 800 ms) |
| `--scroll-loop` | Loop scrolling (default: enabled) |
| `--refresh-rate-hz <Hz>` | Refresh rate (default: 20 Hz) |
| `--display-service <name>` | iceoryx2 display service name |
| `--state-event-service <name>` | iceoryx2 state event service |
| `--state-blackboard-service <name>` | iceoryx2 state blackboard service |
| `--state-control-service <name>` | iceoryx2 state control service |
| `--alert-event-service <name>` | iceoryx2 alert event service |

## Display Interface

| Command | Description |
| --- | --- |
| `SetTitleLine` | Set the top title line (static text, max 256 chars) |
| `SetContentLine` | Set the bottom content line (supports scrolling, max 256 chars) |
| `ClearContentLine` | Clear the bottom content line |

## Dependencies

- Boost.Asio (serial communication)
- iceoryx2 (request-response, event, blackboard)
- GNU Unifont (`.hex` format bitmap font)
