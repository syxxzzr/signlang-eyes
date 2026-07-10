# Telemetry Service

[简体中文](README.md) | [English](README.en.md)

定位与遥测上报模块。通过串口读取 GNSS 模块的 NMEA 定位数据，解析位置信息并通过 MQTT 上报。同时监听 iceoryx2 告警事件，将危险告警发布到独立 MQTT 主题。

## 功能

- 通过 UART 串口读取 GNSS 模块的 NMEA 0183 语句
- 解析 `$GPGGA` 和 `$GPRMC` 语句，提取经纬度、高度、速度、航向等定位信息
- 将位置数据以 JSON 格式通过 MQTT 发布到可配置的主题
- 监听 iceoryx2 告警事件，触发后将告警信息以 JSON 格式发布到独立 MQTT 主题
- 支持 MQTT QoS 0/1/2 和 Retain 消息

## 数据流

```text
GNSS 串口 → NmeaPositionParser → PositionFix → MQTT 位置 JSON
                                                  ↓
iceoryx2 Alert Event → EventListener → MQTT 告警 JSON
```

## 命令行参数

| 参数 | 说明 |
| --- | --- |
| `-d, --device <path>` | GNSS 串口设备（默认 `/dev/ttyS9`） |
| `--baud-rate <rate>` | 串口波特率（默认 9600） |
| `--mqtt-host <host>` | MQTT Broker 地址（默认 `127.0.0.1`） |
| `--mqtt-port <1-65535>` | MQTT Broker 端口（默认 1883） |
| `--mqtt-client-id <id>` | MQTT 客户端 ID |
| `--mqtt-topic <topic>` | 位置数据 MQTT 主题（默认 `signlang/position`） |
| `--alert-event-service <name>` | iceoryx2 告警事件服务名 |
| `--alert-mqtt-topic <topic>` | 告警 MQTT 主题（默认 `signlang/alert`） |
| `--mqtt-username <user>` | MQTT 用户名 |
| `--mqtt-password <pass>` | MQTT 密码 |
| `--mqtt-keep-alive <sec>` | MQTT Keep Alive 间隔（默认 30 s） |
| `--mqtt-qos <0|1|2>` | MQTT QoS 级别（默认 0） |
| `--mqtt-retain` | 启用 MQTT Retain 标志 |

## NMEA 解析

`NmeaPositionParser` 基于 [minmea](https://github.com/kosma/minmea) 库解析 NMEA 0183 语句，支持的语句类型：

- `$GPGGA` — GPS 固定数据（时间、经纬度、高度、卫星数、HDOP）
- `$GPRMC` — 推荐最小定位信息（时间、经纬度、速度、航向）

解析状态包括：无数据、无效语句、不支持语句、解析失败、无定位、无效坐标、有效定位。

## MQTT 上报

- 位置 JSON 包含：`timestamp`、`latitude`、`longitude`、`altitude`、`speed`、`track`、`satellites`、`hdop`
- 告警 JSON 包含：`timestamp`、`alert_type`、`message`
- 使用固定容量队列（64 条）缓冲待发送消息，每 20 ms 排空最多 32 条
- 两个独立 `boost::asio::io_context`：一个处理串口 I/O，一个处理 MQTT 连接

## 依赖

- Boost.Asio / Boost.MQTT5 / Boost.JSON
- minmea（NMEA 0183 解析）
- iceoryx2（Event）
