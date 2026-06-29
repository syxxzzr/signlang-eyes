# signlang_manager — BLE Handpose Stream and Gesture Library Gateway

## Overview

`signlang_manager` exposes a BLE GATT access point for external tools:

- streams latest `handpose_det` results through BLE notifications
- includes newly recognized `signlang_det` results in the next handpose stream notification
- forwards gesture list/add/delete requests to `signlang_det`
- validates and gates BLE uploads before sending raw handpose recordings to `signlang_det`

`signlang_det` owns SQLite prototype storage, upload feature extraction, BiLSTM encoding, DTW representative selection,
and prototype reloads. At startup `signlang_manager` verifies the configured BlueZ adapter, powers it on if needed,
and exits with an error if GATT
application or LE advertisement registration fails.

## BLE Service

Custom service UUID:

```text
3b5f1000-4ad2-4f53-9a65-6f6d65796573
```

Characteristics:

| UUID | Direction | Flags | Description |
|------|-----------|-------|-------------|
| `3b5f1001-4ad2-4f53-9a65-6f6d65796573` | external -> device | write, write-without-response | command packets and upload chunks |
| `3b5f1002-4ad2-4f53-9a65-6f6d65796573` | device -> external | notify, read | responses and handpose stream packets |

Payloads use the versioned packet format in `protocol.{hpp,cpp}`. Larger notifications are split by
`--max-notify-payload`; the receiver should reassemble packets using the protocol header length and payload length.
Only one BLE client may subscribe to streaming notifications at a time. Additional `StartNotify` attempts are rejected
until the current streaming client calls `StopNotify` or its D-Bus owner disappears after disconnect.

## Commands

| Command | ID | Purpose |
|---------|----|---------|
| `GetCapabilities` | `0x0001` | Returns protocol/model/stream status |
| `SetStreamConfig` | `0x0101` | Enables or disables handpose streaming |
| `ListGestures` | `0x0201` | Lists gesture id/name/enabled/sample count |
| `AddGestureBegin` | `0x0202` | Starts an upload session |
| `AddGestureChunk` | `0x0203` | Writes an upload chunk |
| `AddGestureCommit` | `0x0204` | Encodes uploaded handpose frames and stores samples |
| `AddGestureAbort` | `0x0205` | Cancels current upload |
| `DeleteGesture` | `0x0206` | Deletes by id or name |
| `GetStatus` | `0x0301` | Returns current status |

No application-level authentication is implemented.

## Upload Format

`AddGestureBegin` declares a transfer id, total byte size, replace flag, and gesture name. `AddGestureChunk` writes byte
ranges. `AddGestureCommit` fails unless every byte in the declared upload has arrived.

The uploaded blob contains:

```text
u32 frame_count
repeat frame_count:
  u32 frame_payload_size
  wire_handpose_frame payload
```

Each frame payload is the raw-f32 wire format produced by `wire_handpose.{hpp,cpp}`.

During commit, the manager:

1. verifies all BLE upload chunks arrived
2. decodes each wire handpose frame into `HandPoseFrameMetadata` plus `HandPoseDetection` values
3. forwards the decoded frames to `signlang_det` over the gesture-management request-response service
4. returns the stored gesture id from `signlang_det`

## Command-Line Parameters

Relative paths are resolved from the installation root. For installed module executables under `bin/`, the runtime root is the parent directory, so defaults like `models/…`, `conf/…`, and `log/…` do not depend on the shell current working directory.

All module executables also accept `--log-file <path>` and `--log-rotate-size <bytes>`; the launcher supplies these automatically when it starts modules.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--input-service` | required | handpose iceoryx2 service |
| `--signlang-result-service` | required | signlang result iceoryx2 service |
| `--gesture-management-service` | required | signlang_det gesture management request-response service |
| `--bluetooth-name` | `SignLang Eyes` | BLE advertising local name |
| `--adapter-path` | `/org/bluez/hci0` | BlueZ adapter object path |
| `--subscriber-buffer` | `2` | iceoryx2 handpose queue size |
| `--stream-fps` | `30` | max BLE stream frame rate |
| `--max-notify-payload` | `180` | notification chunk size |
| `--enable-streaming-by-default` | false | stream as soon as a client subscribes |

## Running Board BLE Check

Run these on the target board before relying on BLE:

```bash
bluetoothctl list
bluetoothctl show
bluetoothctl power on
bluetoothctl advertise on
```

Then run the installed stack and inspect logs:

```bash
install/launcher --config conf/conf.toml
tail -f log/*signlang_manager*.log
```

Expected manager log line:

```text
BLE GATT service registered on /org/bluez/hci0
```

From a phone or Linux central, scan for the configured local name and connect. If registration fails, capture:

```bash
bluetoothctl show
ps -ef | grep bluetoothd
journalctl -u bluetooth -n 100 --no-pager
```
