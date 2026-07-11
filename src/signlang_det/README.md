# Sign Language Detection

[简体中文](README.md) | [English](README.en.md)

手语识别核心模块。从手部关键点提取时序特征，通过 BiLSTM 编码器（RKNN NPU）与 DTW 原型匹配实现手语词汇识别，同时提供手势库管理功能。

## 功能

- 从 iceoryx2 订阅手部关键点检测结果
- 特征提取：归一化关键点坐标，计算双手相对位置和运动特征，生成 168 维（单手）或 336 维（双手）特征向量
- BiLSTM 编码：通过 RKNN NPU 运行双向 LSTM 编码器，将时序特征序列映射为嵌入向量
- DTW 匹配：在原型库中搜索最匹配的手势原型，输出识别结果
- 原型数据库管理：SQLite 持久化存储手势原型，支持运行时增删改查
- 通过 iceoryx2 Request-Response 提供原型重载和手势库管理接口
- 重复检测抑制和连续命中过滤降低误识别率

## 处理管线

```text
HandPoseDetection[] → FeatureExtractor（归一化/168维） → KeypointRingBuffer（时序窗口）
                                                                ↓
                                                         BiLSTM Encoder（RKNN / NPU）
                                                                ↓
                                                         DTW Matcher → PrototypeStore（SQLite）
                                                                ↓
                                                         SignlangResult → iceoryx2
```

## 命令行参数

| 参数 | 说明 |
| --- | --- |
| `-i, --input-service <name>` | iceoryx2 手部数据输入服务（必填） |
| `-o, --output-service <name>` | iceoryx2 识别结果输出服务（必填） |
| `--prototype-control-service <name>` | 原型控制服务名 |
| `--gesture-management-service <name>` | 手势管理服务名 |
| `-m, --model <path>` | BiLSTM 编码器 RKNN 模型路径 |
| `--prototypes <path>` | 手势原型 SQLite 数据库路径 |
| `--sequence-length <frames>` | 滑窗帧数（默认 30） |
| `--overlap-ratio <0.0-0.9>` | 窗口重叠比例（默认 0.2） |
| `--min-confidence <0.0-1.0>` | 关键点最低置信度（默认 0.3） |
| `--motion-weight <0.0-1.0>` | 运动特征权重（默认 0.0） |
| `--dtw-window-ratio <0.0-1.0>` | DTW 窗口限制比例（默认 0.5） |
| `--confidence-threshold <0.0-1.0>` | 识别阈值（默认 0.6） |
| `--confidence-margin <0.0-1.0>` | 首位与次位置信度差值要求（默认 0.1） |
| `--duplicate-suppression-ms <ms>` | 重复结果抑制时长（默认 1000） |
| `--upload-window-overlap <ratio>` | 上传序列切分重叠（默认 0.5） |
| `--max-representative-samples <n>` | 每手势最大原型样本数（默认 3） |
| `--consecutive-hit-windows <n>` | 连续命中窗口数要求（默认 2） |
| `--subscriber-buffer <n>` | iceoryx2 订阅者队列大小 |
| `--npu-core <mask>` | NPU 核心掩码 |

## IPC 接口

| 接口 | 模式 | 说明 |
| --- | --- | --- |
| 识别结果输出 | Publish-Subscribe | 向 dataflow_dispatcher 发布 SignlangResult |
| 原型控制 | Request-Response | 支持 ReloadPrototypes 和 GetStatus 命令 |
| 手势管理 | Request-Response | 支持手势增删改查（8 种命令） |

## 手势管理

通过 `GestureManagementService` 处理来自 `signlang_manager`（BLE 入口）的请求：

- `ListGestures` — 列出所有已注册手势
- `AddGestureBegin/Chunk/Commit/Abort` — 分块上传新手势样本
- `DeleteGestureById/DeleteGestureByName` — 删除指定手势
- `GetStatus` — 查询当前词库状态

新样本上传后自动编码并写入 SQLite 原型库。

## 依赖

- RKNN API（BiLSTM 编码器推理）
- SQLite / SQLiteCpp（原型库持久化）
- iceoryx2（Publish-Subscribe、Request-Response）
