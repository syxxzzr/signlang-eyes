# Peripheral Service

[简体中文](README.md) | [English](README.en.md)

外设控制与服务模块。通过串口驱动 OLED 显示屏、振动马达和按键，并提供 iceoryx2 显示接口供其他模块更新屏幕内容。

## 功能

- 串口通信：通过 UART 与外设控制器通信，支持可配置的波特率、数据位、停止位、校验和流控
- OLED 显示：使用 GNU Unifont 点阵字体渲染文本，支持双行显示（标题行 + 内容行），内容行支持硬件滚动
- 按键交互：单击循环切换应用状态，双击触发告警事件
- 振动控制：在 `DangerousSound` 状态下激活振动马达
- 通过 iceoryx2 Request-Response 暴露显示接口，接受 SetTitleLine、SetContentLine、ClearContentLine 命令
- 订阅 iceoryx2 状态事件和黑板，同步当前应用状态

## 显示架构

```text
IpcDisplayServer（iceoryx2） → DisplayWorker（定时刷新）
                                  ↓
                            ScrollingDisplay（双行管理）
                                  ↓
                            TextRenderer + HexFont（Unifont 渲染）
                                  ↓
                            SerialTransport（UART 协议）
                                  ↓
                            OLED 外设
```

## 命令行参数

| 参数 | 说明 |
| --- | --- |
| `-d, --device <path>` | 串口设备路径（默认 `/dev/ttyS3`） |
| `--baud-rate <rate>` | 串口波特率（默认 115200） |
| `--data-bits <5-8>` | 数据位（默认 8） |
| `--stop-bits <1|2>` | 停止位（默认 1） |
| `--parity <none|odd|even>` | 校验位（默认 `none`） |
| `--flow-control <none|software|hardware>` | 流控模式（默认 `none`） |
| `--font-file <path>` | GNU Unifont .hex 文件路径 |
| `--display-width <px>` | 显示宽度（默认 128） |
| `--display-height <px>` | 显示高度（默认 32，需为 8 的倍数） |
| `--font-size <px>` | 字号（默认 16） |
| `--char-spacing <px>` | 字符间距（默认 1） |
| `--line-gap <px>` | 行间距（默认 0） |
| `--scroll-speed-px-per-sec <rate>` | 滚动速度（默认 13 px/s） |
| `--scroll-pause-ms <ms>` | 滚动暂停时长（默认 800 ms） |
| `--scroll-loop` | 循环滚动（默认开启） |
| `--refresh-rate-hz <Hz>` | 刷新率（默认 20 Hz） |
| `--display-service <name>` | iceoryx2 显示服务名 |
| `--state-event-service <name>` | iceoryx2 状态事件服务 |
| `--state-blackboard-service <name>` | iceoryx2 状态黑板服务 |
| `--state-control-service <name>` | iceoryx2 状态控制服务 |
| `--alert-event-service <name>` | iceoryx2 告警事件服务 |

## 显示接口

| 命令 | 说明 |
| --- | --- |
| `SetTitleLine` | 设置顶部标题行（静态文本，最长 256 字符） |
| `SetContentLine` | 设置底部内容行（支持滚动，最长 256 字符） |
| `ClearContentLine` | 清空底部内容行 |

## 依赖

- Boost.Asio（串口通信）
- iceoryx2（Request-Response、Event、Blackboard）
- GNU Unifont（`.hex` 格式点阵字体）
