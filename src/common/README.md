# Common

[简体中文](README.md) | [English](README.en.md)

共享基础设施库，为所有模块提供运行时工具、日志系统、音频环形缓冲区和 iceoryx2 IPC 辅助函数。

## 组件一览

| 组件 | 文件 | 说明 |
| --- | --- | --- |
| 模块运行时 | `runtime.hpp` | 通用的 `main()` 包装器，处理信号、CPU 亲和性绑定和可执行路径解析 |
| 日志系统 | `logging.hpp` | 基于 spdlog 的日志初始化，支持 stdout + 文件滚动输出的双 sink 模式 |
| 日志 CLI | `logging_cli.hpp` | 日志相关的命令行选项解析（`--log-file`、`--log-level`、`--log-rotate-size`） |
| IPC 工具 | `ipc_utils.hpp` | iceoryx2 服务名解析、节点创建、发布订阅和请求响应的辅助模板 |
| CPU 亲和性 | `cpu_affinity.hpp` | 将当前线程绑定到指定 CPU 核心 |
| 音频环形缓冲 | `audio_ring_buffer.hpp` | 线程安全的音频帧环形缓冲区，用于音频前后端解耦 |
| 时间工具 | `time.hpp` | 高精度时间戳获取和转换 |
| 定长字符串 | `fixed_string.hpp` | 编译期定长字符串，用于 IPC 类型名注册 |

## 模块运行时

`runtime::run_module()` 提供标准化的模块入口流程：

1. 解析命令行参数（通过 `cxxopts`）
2. 切换到运行时根目录（`install/`）
3. 初始化日志系统
4. 可选绑定 CPU 核心
5. 注册 SIGINT/SIGTERM 信号处理器
6. 执行模块主循环
7. 统一异常捕获与退出码返回

## 日志系统

- 同时输出到 stdout（带颜色）和可选的滚动日志文件
- 日志格式：`[日期T时间.毫秒时区] [级别] [模块名] 消息`
- 支持配置单文件最大大小（`rotate_size`）和保留文件数（`retain_files`）
- 各模块可通过 TOML 中的 `log_level` 独立覆盖全局日志级别

## iceoryx2 IPC 辅助

- `create_ipc_node()` — 创建禁用信号处理的 iceoryx2 节点，适用于已自行管理信号的模块
- `receive_latest_sample()` — 从订阅者获取最新样本（跳过积压的旧数据）
- `send_request_and_wait_for_response()` — 发送请求并带超时重试的等待响应
- `service_name_from_string()` — 字符串到 `iox2::ServiceName` 的安全转换
