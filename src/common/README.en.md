# Common

[简体中文](README.md) | [English](README.en.md)

Shared infrastructure library providing runtime utilities, logging, an audio ring buffer, and iceoryx2 IPC helpers for all modules.

## Component Overview

| Component | File | Description |
| --- | --- | --- |
| Module Runtime | `runtime.hpp` | Generic `main()` wrapper handling signals, CPU affinity binding, and executable path resolution |
| Logging | `logging.hpp` | spdlog-based log initialization with dual sinks: stdout + rotating file |
| Logging CLI | `logging_cli.hpp` | CLI option parsing for logging (`--log-file`, `--log-level`, `--log-rotate-size`) |
| IPC Utilities | `ipc_utils.hpp` | iceoryx2 service name parsing, node creation, and helper templates for pub-sub and request-response |
| CPU Affinity | `cpu_affinity.hpp` | Bind the calling thread to a specific CPU core |
| Audio Ring Buffer | `audio_ring_buffer.hpp` | Thread-safe audio frame ring buffer for decoupling audio frontends and backends |
| Time Utilities | `time.hpp` | High-resolution timestamp acquisition and conversion |
| Fixed String | `fixed_string.hpp` | Compile-time fixed-length string for IPC type name registration |

## Module Runtime

`runtime::run_module()` provides a standardized module entry flow:

1. Parse CLI arguments (via `cxxopts`)
2. Change to the runtime root directory (`install/`)
3. Initialize the logging system
4. Optionally bind to a CPU core
5. Register SIGINT/SIGTERM signal handlers
6. Execute the module main loop
7. Catch exceptions uniformly and return an exit code

## Logging

- Simultaneously outputs to stdout (colorized) and an optional rotating log file
- Log format: `[YYYY-MM-DDThh:mm:ss.msTZ] [level] [module_name] message`
- Configurable max file size (`rotate_size`) and retained file count (`retain_files`)
- Each module can override the global log level via the `log_level` key in its TOML section

## iceoryx2 IPC Helpers

- `create_ipc_node()` — Creates an iceoryx2 node with signal handling disabled, suitable for modules that manage their own signals
- `receive_latest_sample()` — Retrieves the latest sample from a subscriber, skipping stale backlog
- `send_request_and_wait_for_response()` — Sends a request and waits for a response with timeout and retry
- `service_name_from_string()` — Safe conversion from `std::string` to `iox2::ServiceName`
