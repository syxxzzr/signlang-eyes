# SignLang Eyes

[![License](https://img.shields.io/badge/license-Apache--2.0-0f766e)](LICENSE)

[简体中文](README.md) | [English](README.en.md)

面向听障人士的端侧实时辅助系统：在 RK3588 上融合双手手语识别、语音识别与合成、危险声音提醒、显示交互和位置遥测。

本项目还包含一个开源的手语词库管理工具，用于使用 `Web Bluetooth` 技术连接设备并管理手语词库，详细内容请参见 [https://github.com/syxxzzr/signlang-eyes-configurator]()

> 本项目目前仅支持 `aarch64` / `arm64` / `armv8`，并以 Rockchip RK3588 为实际适配平台。完整运行还需要摄像头、音频设备、RKNN 模型和相应外设；它不是可以直接在普通 x86_64 电脑上运行的桌面应用。

## 项目简介

SignLang Eyes 将摄像头、麦克风、扬声器、显示屏和通信模块组织成一组独立进程，通过 iceoryx2 共享内存 IPC 传递音视频帧和识别结果。计算密集型模型尽量使用 RK3588 NPU，图像转换与缩放使用 RGA，业务流程则由状态机统一控制。

系统围绕四类场景构建：

- **手语交流**：检测双手 21 点关键点，通过动作分段、时序编码和两级原型匹配识别手语，并通过语音播报结果。
- **手语 AI**：将连续手语结果组合成提示词，调用 OpenAI 兼容接口，并在屏幕上显示回复。
- **语音转写**：使用 Whisper 将中文或英文语音转为文字，显示在外接 OLED 上。
- **环境感知**：使用 YAMNet 识别危险声音，通过状态切换、振动和 MQTT 告警提醒用户。

除此之外，项目还包含 BLE 手势库管理、串口按键与显示控制、GNSS 定位和 MQTT 遥测等设备侧能力。

## 核心能力

| 领域 | 能力 | 主要实现 |
| --- | --- | --- |
| 手语识别 | 双手检测、动作分段、时序特征编码、可动态扩展词库 | MediaPipe 手部模型、Temporal Encoder、DTW、SQLite |
| 语音交互 | 中英文语音识别、中文语音合成 | Whisper、Piper、cpp-pinyin |
| 环境安全 | 环境声音分类、危险状态、振动与远程告警 | YAMNet、状态机、MQTT |
| 端侧推理 | 多 NPU 核选择、模型级核心分配 | RKNN Runtime |
| 音视频前端 | ALSA 采集与声源定位、V4L2 采集、硬件图像处理 | ALSA、FFTW3、V4L2、RGA、libjpeg-turbo |
| 设备交互 | OLED 文本显示、串口按键、BLE GATT、GNSS | 串口协议、BlueZ、GLib/GIO、minmea |
| 进程协作 | 零拷贝数据传输、事件通知、黑板和请求响应 | iceoryx2 |

## 系统架构

`launcher` 读取 TOML 配置，按依赖顺序启动并监督 13 个子进程。任一模块启动失败或意外退出时，launcher 会停止整组进程，并根据 `restart_attempts` 策略重新启动。

```text
麦克风
  └─ audio_frontend ─┬─ speech_asr ───────────────┐
                     └─ env_sound_det ──┐         │
                                        ▼         ▼
摄像头                             state_machine  dataflow_dispatcher
  └─ video_frontend                  ▲  │         ├─ speech_tts ──> 扬声器
       └─ handpose_det ─┬─ signlang_det ──────────┤
                        └─ signlang_manager       ├─ llm_client ──> AI 服务
                              ▲                   └─ peripheral_service ──> OLED / 振动
                              └──── BLE 手势管理              │
                                                             ▼
GNSS ───────────────────────────────> telemetry_service ──> MQTT
```

模块间服务名由 launcher 固定，不能通过 TOML 修改。音视频数据使用发布订阅；状态、控制和业务调用分别使用事件、黑板与请求响应模式。

## 应用状态

系统默认进入 `Normal`。基础状态可以在 `Normal`、`Asr`、`SignLanguageChat` 和 `SignLanguageAi` 之间切换，`DangerousSound` 是带超时恢复的特殊状态。

| 状态 | 系统行为 |
| --- | --- |
| `Normal` | 保持环境声音检测，语音与手语推理处于空闲状态 |
| `Asr` | 启用 Whisper 语音识别，并在 OLED 上显示转写结果 |
| `SignLanguageChat` | 启用手部与手语识别，将识别结果交给 TTS 播报 |
| `SignLanguageAi` | 累积手语片段，调用 OpenAI 兼容接口并显示回复 |
| `DangerousSound` | 显示危险提示、启用振动并产生遥测告警，超时后恢复基础状态 |

## 运行要求

### 目标硬件

- Rockchip RK3588 或兼容的 aarch64 Linux 设备
- 支持 V4L2 的摄像头
- 支持 ALSA 的麦克风和播放设备
- 可用的 RKNN Runtime 与 RGA 驱动
- 完整系统默认还会使用 BlueZ BLE 适配器、OLED/按键外设串口和 GNSS 串口
- 使用手语 AI 或遥测时，需要可访问的 OpenAI 兼容服务或 MQTT Broker

开发阶段可以单独启动模块，但通过 `launcher` 运行完整系统时，配置中涉及的设备和运行资产必须可用，否则失败模块会触发整组进程重启。

### 构建环境

- CMake 3.20+
- Conan 2.x
- GCC 11+ 与 C++17
- `aarch64-linux-gnu-gcc`、`aarch64-linux-gnu-g++`
- 目标系统 sysroot，默认位置为 `/root/sysroot`

系统库还包括 ALSA、OpenSSL、GLib/GIO；其余主要 C++ 依赖通过 Conan 管理。

## 快速开始

完整的开发环境准备、Conan 配置和交叉编译说明见 [docs/quick_start.md](docs/quick_start.md)。以下命令只展示主流程，均在仓库根目录执行。

### 1. 准备 Conan

```bash
export CONAN_HOME="$PWD/.conan"                   # 可选：Conan缓存隔离
export SIGNLANG_AARCH64_SYSROOT=/path/to/sysroot

conan profile detect --force
conan export conan/recipes/cpp-pinyin
conan export conan/recipes/iceoryx2
conan export conan/recipes/librga
conan export conan/recipes/rknn-runtime
```

### 2. 安装依赖并交叉编译

```bash
conan install . \
  -of build-aarch64 \
  -pr:h conan/profiles/linux-aarch64-gcc \
  -pr:b default \
  --build=missing

cmake -S . -B build-aarch64 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=build-aarch64/conan_toolchain.cmake \
  -DCMAKE_INSTALL_PREFIX=install

cmake --build build-aarch64 --target install -j"$(nproc)"
```

在 aarch64 设备上原生构建时，同样需要先通过 Conan 准备依赖，再使用生成的 `conan_toolchain.cmake` 配置 CMake。

### 3. 准备运行资产

为避免提交大型或部署相关文件，`models/`、`*.sqlite` 和 `*.hex` 已被 `.gitignore` 排除，不随干净克隆提供。启动前至少需要按配置准备：

```text
models/
├── whisper/      Whisper 编码器、解码器、词表和 Mel 滤波器
├── piper/        Piper 编码器、解码器和声音配置
├── yamnet/       YAMNet RKNN 模型与类别表
├── mediapipe/    手掌检测与手部关键点 RKNN 模型
└── signlang/     手语时序 RKNN 编码器

conf/
├── conf.toml
├── system_prompt.txt
├── unifont-17.0.05.hex
└── prototypes.sqlite
```

发布镜像可以在 CMake 配置时加入 `-DSIGNLANG_RUNTIME_ASSETS_REQUIRED=ON`，让缺失资产直接导致配置失败，而不是仅输出警告。

手语识别还需要已经录入样本的 `prototypes.sqlite`。词汇通过 BLE 管理接口上传后，由 `signlang_det` 使用与实时识别相同的处理管线编码并写入数据库；时序编码器本身不包含固定词表。

### 4. 配置并启动

先根据目标设备修改 [conf/conf.toml](conf/conf.toml) 中的摄像头、音频、串口、NPU 核心、MQTT 和 LLM 参数，然后将安装目录部署到 RK3588 设备：

```bash
install/launcher
```

也可以指定配置文件：

```bash
install/launcher --config /path/to/conf.toml
```

launcher 会切换到安装根目录，因此 `conf/`、`models/` 和 `log/` 等相对路径不依赖启动命令所在目录。

## 配置说明

[conf/conf.toml](conf/conf.toml) 列出了当前支持的配置项和默认值。常用配置包括：

- `[launcher]`：整组进程失败后的重启次数，`-1` 表示无限重试。
- `[logging]`：全局日志级别、单文件大小和保留数量；各模块可覆盖日志级别。
- `[audio_frontend]`/`[video_frontend]`：设备节点、采样格式、分辨率、镜像和旋转。
- `[speech_asr]`/`[speech_tts]`：模型路径、语言、播放设备和 NPU 核心。
- `[handpose_det]`/`[signlang_det]`：检测、跟踪、时序窗口、DTW 和输入质量参数。
- `[signlang_manager]`：BLE 名称、适配器、流帧率和上传限制。
- `[telemetry_service]`：GNSS 串口与 MQTT 连接参数。
- `[peripheral_service]`：串口、字体、OLED 尺寸和滚动显示参数。
- `[llm_client]`：OpenAI 兼容接口地址、密钥、模型和超时。

## 模块说明

| 模块                    | 功能                               |
|-----------------------|----------------------------------|
| `launcher`            | 读取配置、启动 13 个子进程、监控与整组重启          |
| `state_machine`       | 管理基础状态、危险声音特殊状态和状态控制             |
| `audio_frontend`      | ALSA 音频采集、重采样、混音和声源定位            | 
| `video_frontend`      | V4L2 采集、MJPEG 解码和 RGA 图像处理       | 
| `speech_asr`          | Whisper 滑动窗口语音识别                 | 
| `speech_tts`          | Piper 中文语音合成与 ALSA 播放            | 
| `env_sound_det`       | YAMNet 环境声音识别与危险状态触发             |
| `handpose_det`        | 手掌检测、双手关键点、跟踪与平滑                 | 
| `signlang_det`        | 动作分段、168 维特征、时序编码、两级匹配和原型存储 |
| `signlang_manager`    | BLE 手部数据流和手势词库管理入口               |
| `dataflow_dispatcher` | 按状态路由 ASR、手语、TTS、LLM 和显示数据       | 
| `peripheral_service`  | 串口按键、OLED 文本、振动和告警事件             |
| `telemetry_service`   | NMEA 定位解析和 MQTT 位置/告警上报          | 
| `llm_client`          | OpenAI 兼容的 Chat Completions 请求服务 | 

模块 README 中的命令行参数适合单模块调试；日常完整系统启动建议统一使用 `launcher` 和 TOML 配置。

## 项目结构

```text
signlang-eyes/
├── CMakeLists.txt             根构建配置和安装规则
├── conanfile.txt              Conan 依赖清单
├── conan/
│   ├── profiles/              aarch64 交叉编译 profile
│   └── recipes/               项目维护的 Conan recipes
├── conf/                      TOML 配置和系统提示词
├── docs/                      快速开始等项目文档
├── models/                    本地运行模型，不纳入版本控制
└── src/
    ├── common/                共享运行时、日志和 IPC 工具
    ├── launcher/              进程编排
    ├── state_machine/         状态管理
    ├── audio_frontend/        音频采集
    ├── video_frontend/        视频采集
    ├── speech_asr/            语音识别
    ├── speech_tts/            语音合成
    ├── env_sound_det/         环境声音识别
    ├── handpose_det/          手部关键点检测
    ├── signlang_det/          手语识别
    ├── signlang_manager/      BLE 与手势库管理
    ├── dataflow_dispatcher/   业务数据路由
    ├── peripheral_service/    显示与外设控制
    ├── telemetry_service/     定位与遥测
    └── llm_client/            LLM 接口
```

## 特别鸣谢

感谢以下开源项目及其维护者。本项目能够在端侧完成音视频采集、模型推理、进程通信和设备交互，离不开这些基础工作：

- **进程通信与异步基础设施**：[iceoryx2](https://github.com/eclipse-iceoryx/iceoryx2)、[Boost](https://www.boost.org/)（JSON、Container、Asio、Beast、MQTT5）、[OpenSSL](https://www.openssl.org/)。
- **AI 推理与硬件加速**：[ONNX Runtime](https://onnxruntime.ai/)、[RKNN Runtime](https://github.com/airockchip/rknn-toolkit2)、[Rockchip RGA](https://github.com/airockchip/librga)、[libjpeg-turbo](https://libjpeg-turbo.org/)。
- **音频、数据与设备支持**：[ALSA](https://www.alsa-project.org/)、[FFTW](https://www.fftw.org/)、[SQLite](https://www.sqlite.org/)、[SQLiteCpp](https://github.com/SRombauts/SQLiteCpp)、[GLib](https://docs.gtk.org/glib/) / [GIO](https://docs.gtk.org/gio/)、[BlueZ](https://www.bluez.org/)、[minmea](https://github.com/kosma/minmea)。
- **工程与通用组件**：[CMake](https://cmake.org/)、[Conan](https://conan.io/)、[spdlog](https://github.com/gabime/spdlog)、[fmt](https://github.com/fmtlib/fmt)、[toml++](https://github.com/marzer/tomlplusplus)、[cxxopts](https://github.com/jarro2783/cxxopts)、[cpp-pinyin](https://github.com/wolfgitpr/cpp-pinyin)。
- **模型与算法生态**：[OpenAI Whisper](https://github.com/openai/whisper)、[Google YAMNet](https://github.com/tensorflow/models/tree/master/research/audioset/yamnet)、[Google MediaPipe](https://github.com/google-ai-edge/mediapipe)、[Piper](https://github.com/rhasspy/piper)。

各第三方库、模型和相关数据仍遵循其各自的许可证、版权与使用条款。感谢所有贡献者持续维护这些项目并开放成果。
