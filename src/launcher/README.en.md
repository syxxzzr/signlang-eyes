# Launcher

[简体中文](README.md) | [English](README.en.md)

Process orchestration and supervision module. Reads a TOML configuration, launches all child processes in dependency order, monitors their health, and performs a full process-group restart on abnormal exits.

## Features

- Parses the TOML configuration file and extracts per-module runtime parameters
- Launches 13 child processes in a fixed order: state machine, frontend capture, inference engines, data dispatcher, and peripheral services
- On child process failure, sends SIGTERM to all children (3-second grace period), escalating to SIGKILL if necessary
- Supports configurable restart policy: `restart_attempts` of `-1` for unlimited retries, or a positive integer to cap restart attempts
- Generates per-module log files with UTC timestamps and performs old log rotation/cleanup
- Detects and warns about hardcoded iceoryx2 IPC service names in the TOML configuration to prevent user misconfiguration

## CLI Arguments

| Argument | Description |
| --- | --- |
| `--config <path>` | Path to the TOML configuration file (default: `conf/conf.toml`) |
| `--help` | Display help information |

## Design Notes

- **IPC Service Naming**: The launcher hardcodes all inter-module iceoryx2 service names at compile time. These names cannot be changed via TOML. Any IPC service name keys found in the configuration are detected and flagged with a warning. This constraint guarantees a zero-configuration communication topology with consistent naming across the system.
- **exec + pipe error reporting**: Child processes use `pipe2(O_CLOEXEC)` to relay `errno` back to the parent on exec failure, preventing silent launch failures
- **SIGCHLD default handling**: The launcher resets SIGCHLD to its default disposition (SIG_DFL) and polls explicitly via `waitpid(WNOHANG)`
- **Relative path resolution**: Configuration file paths are resolved relative to the invocation directory first, falling back to the runtime root directory
- **Log isolation**: Each module writes to a dedicated log file named `{UTC_timestamp}-{module_name}.log`
