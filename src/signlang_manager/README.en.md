# Sign Language Manager

[简体中文](README.md) | [English](README.en.md)

BLE gesture library management and hand data streaming module. Exposes real-time hand keypoint streaming and sign language vocabulary management through a Bluetooth Low Energy (BLE) GATT service, acting as a bridge between external devices (e.g., a mobile app) and the system's internal iceoryx2 IPC.

## Features

- Registers a BlueZ BLE GATT server and advertises the device
- Real-time hand data streaming: subscribes to handpose detections and sign language recognition results from iceoryx2, packs them into binary protocol frames, and pushes them via BLE notifications
- Gesture library management: receives BLE client requests (list, add, delete gestures), forwards them to `signlang_det`'s `GestureManagementService` over iceoryx2
- Supports configurable stream frame rate (`--stream-fps`) and notification payload size limits

## Data Flow

```text
BLE Client ←→ BluetoothGattServer (BlueZ D-Bus)
                    ↕
              ManagerService (protocol encode/decode)
                    ↕
    ┌───────────────┼───────────────┐
    ↓                               ↓
iceoryx2 subscriber              iceoryx2 client
(handpose + signlang)           (gesture_management)
```

## Protocol Commands

| Command | Direction | Description |
| --- | --- | --- |
| `GetCapabilities` | Client → Device | Query device capabilities (stream FPS, payload limits) |
| `SetStreamConfig` | Client → Device | Enable/disable hand data streaming |
| `HandposeFrame` | Device → Client | Real-time hand keypoint + sign language result frame |
| `ListGestures` | Client → Device | List all registered gestures |
| `AddGesture` | Client → Device | Chunked upload of a new gesture |
| `DeleteGesture` | Client → Device | Delete a gesture by ID or name |
| `GetStatus` | Client → Device | Query vocabulary and upload status |

## CLI Arguments

| Argument | Description |
| --- | --- |
| `-i, --input-service <name>` | iceoryx2 handpose input service (required) |
| `--signlang-result-service <name>` | iceoryx2 sign language result service |
| `--gesture-management-service <name>` | iceoryx2 gesture management service |
| `--bluetooth-name <name>` | BLE advertising name (default: "SignLang Eyes") |
| `--adapter-path <path>` | BlueZ adapter object path (default: `/org/bluez/hci0`) |
| `--subscriber-buffer <n>` | iceoryx2 subscriber queue size (default: 2) |
| `--stream-fps <rate>` | Maximum BLE stream frame rate (default: 30) |
| `--max-notify-payload <bytes>` | Maximum BLE notification payload (default: 180) |
| `--max-upload-bytes <bytes>` | Maximum gesture upload size (default: 8 MB) |
| `--enable-streaming-by-default` | Start streaming automatically on client subscription |

## Gesture Upload Protocol

Large gesture samples are transferred via a chunked upload protocol:
1. `AddGestureBegin` — Specify gesture name, transfer ID, total size
2. `AddGestureChunk` — Send data chunk
3. `AddGestureCommit` — Verify integrity, commit to `signlang_det` for encoding
4. `AddGestureAbort` — Cancel mid-transfer

## Dependencies

- BlueZ (D-Bus API, BLE GATT server)
- GLib / GIO
- iceoryx2 (publish-subscribe, request-response)
