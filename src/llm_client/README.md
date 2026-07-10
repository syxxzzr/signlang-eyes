# LLM Client

[简体中文](README.md) | [English](README.en.md)

大语言模型接口模块。通过 iceoryx2 请求-响应接口接收提示词，使用 Boost.Beast 发起 HTTP 请求调用 OpenAI 兼容的 Chat Completions API，并返回模型回复。

## 功能

- 通过 iceoryx2 Request-Response 接收来自 dataflow_dispatcher 的 LLM 请求
- 将内置系统提示词（从文件加载）与用户提示词拼接为 Chat Completions 请求体
- 使用 Boost.Beast 进行 HTTPS 通信，调用 OpenAI 兼容 API
- 多线程工作池：每个工作线程独立持有 `boost::asio::io_context` 和 `OpenAiClient` 实例
- 通过无锁 SPSC 环形队列分配工作，轮询调度到空闲工作者

## 处理管线

```text
iceoryx2 LLM 请求 → IpcLlmServer → LlmWorkerPool（SPSC 队列）
                                        ↓
                                  OpenAiClient（Boost.Beast HTTPS）
                                        ↓
                                  LlmCompletionResult → iceoryx2 响应
```

## 命令行参数

| 参数 | 说明 |
| --- | --- |
| `-s, --service <name>` | iceoryx2 LLM 请求-响应服务名（必填） |
| `--base-url <url>` | OpenAI 兼容 API 基础 URL（默认 `https://api.openai.com/v1`） |
| `--api-key <key>` | API 密钥（为空时不发送 Authorization 头） |
| `--model <name>` | 模型名称（默认 `gpt-5.5`） |
| `--system-prompt-file <path>` | 内置系统提示词文件路径（默认 `conf/system_prompt.txt`） |
| `--request-timeout-ms <ms>` | HTTP 请求超时时间（默认 60000 ms） |
| `--concurrency <n>` | 工作线程数（默认 2） |
| `--worker-queue-capacity <n>` | 每个工作者的最大排队请求数（默认 8） |

## 响应状态

| 状态 | 说明 |
| --- | --- |
| `Ok` | 请求成功，返回模型回复文本 |
| `BadRequest` | 请求参数无效（提示词为空或过长） |
| `ApiError` | API 返回非 200 HTTP 状态码 |
| `NetworkError` | HTTP 请求失败或解析错误 |
| `ResponseTooLarge` | 响应体超过最大长度（8192 字符） |

## 设计要点

- URL 解析支持 `http` 和 `https` scheme，自动设置默认端口（80/443）
- 内置系统提示词在启动时从文件加载并注入到每个用户请求中
- SPSC 环形队列实现无锁工作分配，避免互斥开销
- 每个工作者的 io_context 独立运行在专用线程上，天然隔离 HTTP 连接

## 依赖

- Boost.Asio / Boost.Beast / Boost.JSON
- OpenSSL（HTTPS/TLS）
- iceoryx2（Request-Response）
