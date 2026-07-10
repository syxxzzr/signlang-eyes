# Sign Language Manager

[简体中文](README.md) | [English](README.en.md)

BLE 手势库管理与手部数据流模块。通过蓝牙低功耗（BLE）GATT 服务暴露手部关键点实时数据流和手语词汇管理功能，作为外部设备（如手机 App）与系统内部 iceoryx2 IPC 之间的桥梁。

## 功能

- 注册 BlueZ BLE GATT 服务，对外广播设备
- 实时手部数据流：从 iceoryx2 订阅 handpose_det 的关键点数据和 signlang_det 的识别结果，打包为二进制协议帧通过 BLE Notification 推送
- 手势库管理：接收 BLE 客户端的请求（列举、添加、删除手势），通过 iceoryx2 转发给 signlang_det 的 GestureManagementService
- 支持可配置的流帧率（`--stream-fps`）和通知负载大小限制

## 数据流

```text
BLE 客户端 ←→ BluetoothGattServer（BlueZ D-Bus）
                    ↕
              ManagerService（协议编解码）
                    ↕
    ┌───────────────┼───────────────┐
    ↓                               ↓
iceoryx2 订阅                    iceoryx2 客户端
(handpose + signlang)           (gesture_management)
```

## 协议命令

| 命令 | 方向 | 说明 |
| --- | --- | --- |
| `GetCapabilities` | 客户端→设备 | 查询设备能力（流帧率、负载限制） |
| `SetStreamConfig` | 客户端→设备 | 开关手部数据流 |
| `HandposeFrame` | 设备→客户端 | 实时手部+手语结果帧 |
| `ListGestures` | 客户端→设备 | 列出已注册手势 |
| `AddGesture` | 客户端→设备 | 分块上传新手势 |
| `DeleteGesture` | 客户端→设备 | 按 ID 或名称删除手势 |
| `GetStatus` | 客户端→设备 | 查询词库和上传状态 |

## 命令行参数

| 参数 | 说明 |
| --- | --- |
| `-i, --input-service <name>` | iceoryx2 手部数据输入服务（必填） |
| `--signlang-result-service <name>` | iceoryx2 手语识别结果服务 |
| `--gesture-management-service <name>` | iceoryx2 手势管理服务 |
| `--bluetooth-name <name>` | BLE 广播名称（默认 "SignLang Eyes"） |
| `--adapter-path <path>` | BlueZ 适配器对象路径（默认 `/org/bluez/hci0`） |
| `--subscriber-buffer <n>` | iceoryx2 订阅者队列大小（默认 2） |
| `--stream-fps <rate>` | 最大 BLE 流帧率（默认 30） |
| `--max-notify-payload <bytes>` | 单次 BLE 通知最大负载（默认 180） |
| `--max-upload-bytes <bytes>` | 单次手势上传上限（默认 8 MB） |
| `--enable-streaming-by-default` | 客户端订阅后自动开始流式传输 |

## 手势上传协议

大手势样本通过分块上传传输：
1. `AddGestureBegin` — 指定手势名称、传输 ID、总大小
2. `AddGestureChunk` — 发送数据块
3. `AddGestureCommit` — 校验完整性，提交给 signlang_det 编码
4. `AddGestureAbort` — 中途取消

## 依赖

- BlueZ（D-Bus API，BLE GATT 服务）
- GLib / GIO
- iceoryx2（Publish-Subscribe、Request-Response）
