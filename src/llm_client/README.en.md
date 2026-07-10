# LLM Client

[简体中文](README.md) | [English](README.en.md)

Large language model interface module. Receives prompts via an iceoryx2 request-response interface, makes HTTP requests to an OpenAI-compatible Chat Completions API using Boost.Beast, and returns the model's reply.

## Features

- Receives LLM requests from `dataflow_dispatcher` over iceoryx2 request-response
- Concatenates the built-in system prompt (loaded from a file) with the user prompt into a Chat Completions request body
- Uses Boost.Beast for HTTPS communication with an OpenAI-compatible API
- Multi-threaded worker pool: each worker thread holds an independent `boost::asio::io_context` and `OpenAiClient` instance
- Distributes work via a lock-free SPSC ring queue with round-robin scheduling to idle workers

## Processing Pipeline

```text
iceoryx2 LLM request → IpcLlmServer → LlmWorkerPool (SPSC queue)
                                           ↓
                                     OpenAiClient (Boost.Beast HTTPS)
                                           ↓
                                     LlmCompletionResult → iceoryx2 response
```

## CLI Arguments

| Argument | Description |
| --- | --- |
| `-s, --service <name>` | iceoryx2 LLM request-response service name (required) |
| `--base-url <url>` | OpenAI-compatible API base URL (default: `https://api.openai.com/v1`) |
| `--api-key <key>` | API secret key (omits the Authorization header when empty) |
| `--model <name>` | Model name (default: `gpt-5.5`) |
| `--system-prompt-file <path>` | Built-in system prompt file (default: `conf/system_prompt.txt`) |
| `--request-timeout-ms <ms>` | HTTP request timeout (default: 60000 ms) |
| `--concurrency <n>` | Number of worker threads (default: 2) |
| `--worker-queue-capacity <n>` | Maximum queued requests per worker (default: 8) |

## Response Statuses

| Status | Description |
| --- | --- |
| `Ok` | Request succeeded; model reply text returned |
| `BadRequest` | Invalid request parameters (empty or oversized prompt) |
| `ApiError` | API returned a non-200 HTTP status code |
| `NetworkError` | HTTP request failure or parse error |
| `ResponseTooLarge` | Response body exceeds the maximum length (8192 chars) |

## Design Notes

- URL parsing supports both `http` and `https` schemes, with automatic default port assignment (80/443)
- The built-in system prompt is loaded from a file at startup and injected into every user request
- The SPSC ring queue provides lock-free work distribution, avoiding mutex contention
- Each worker's io_context runs independently on a dedicated thread, naturally isolating HTTP connections

## Dependencies

- Boost.Asio / Boost.Beast / Boost.JSON
- OpenSSL (HTTPS/TLS)
- iceoryx2 (request-response)
