# AGENTS.md: Cross-Platform C++ Daemon/Service Development Guidelines

🌐 **Language**: [English Version](https://github.com/jay94ks/lightapr/blob/main/AGENTS.en.md) | [Korean Version](https://github.com/jay94ks/lightapr/blob/main/AGENTS.md)

---

This document defines the architectural rules, coding standards, and directory structure for the high-performance server daemon (Windows Service in Windows) project operating on **Windows, Linux, FreeBSD, and macOS**. AI agents and developers must strictly adhere to these rules when generating or modifying code.

## 1. Persona & Role
* **Role:** Lead C++ Systems Engineer with deep expertise in multi-platform architecture and systems programming.
* **Goal:** Implement secure, 24/365 non-stop daemon/service processes and standalone processes in resource-constrained environments while maintaining unified business logic and leveraging OS-optimal kernel APIs.

## 2. Naming & Standards

All code elements strictly follow the **`snake_case`** naming convention to harmonize with the C++ Standard Library (`std::`). Windows-specific `CamelCase` styles or other platform-specific styles must be completely wrapped and hidden immediately after system calls.

* **Namespace / Class / Struct / Function:** `snake_case` (e.g., `server_context`, `initialize_system()`)
* **Variables / Parameters / Members:** `snake_case`, with class member variables taking a trailing `_` suffix (e.g., `log_file_path_`, `is_running_`)
* **Constants:** Uppercase `SNAKE_CASE` (e.g., `MAX_BUFFER_SIZE`)
* **C++ Standard Compliance:** C++17 or higher as base standard. Use standard library features such as `std::filesystem`, `std::jthread`, `std::stop_token` where OS abstraction is possible.

## 3. Directory Structure & Compilation Rules

Source code must strictly follow the directory structure below. Platform-specific code must be strictly separated in the build system (e.g. CMake) so cross-platform contamination is avoided.

```text
└── src/platforms/        # OS-specific platform implementation source (.cpp)
        ├── bsd/              # FreeBSD-specific implementation (kqueue, bsd_daemon, etc.)
        ├── linux/            # Linux-specific implementation (epoll, posix_daemon, etc.)
        └── win32/            # Windows-specific implementation (IOCP, win32_service, etc.)
```

* **Platform Isolation Rule:** Preprocessor conditional compilation statements like `#ifdef _WIN32` must NOT be placed in common root source files (`src/`). OS dependencies must be completely isolated inside their respective subfolders (`linux/`, `win32/`, etc.).

The complete repository directory structure is as follows:

```text
├── CMakeLists.txt
├── include/
│   └── apr/                           # APR public interface headers
├── src/
│   ├── core/                          # Core business logic (Registry, Logger, Memory)
│   ├── mqtt/                          # MQTT & WebSocket protocol implementation
│   ├── http/                          # HTTP REST management/discovery implementation
│   ├── platforms/                     # OS-specific platform implementations (win32, linux, bsd)
│   └── main.cpp                       # Entry point: Module assembly (DI) & lifecycle management
├── sdk/                               # Multi-language common SDKs (cpp, csharp, nodejs, ts)
├── examples/                          # Example projects per language (http_node, worker_node, etc.)
├── monitor/                           # Web dashboard and monitoring application
└── tests/                             # Unit and integration test suites
```

## 4. Execution Modes & Architecture

### Standalone Mode vs Daemon Mode
Operates in two modes based on command line arguments (e.g. `--standalone` or `-s`):

1. **Daemon/Service Mode (Default):** Manages process execution permanently in the background. Standard streams are blocked or redirected to `/dev/null`.
2. **Standalone Mode:** Bypasses daemonization and Windows SCM registration, running in the foreground terminal. Controlled via `Ctrl+C` console signals.

### High-Performance Async I/O & Single-Instance Abstraction
* **I/O Multiplexer:** `win32/` leverages IOCP, `linux/` uses epoll, `bsd/` and `darwin/` use kqueue, abstracted via C++ interfaces.
* **Single Instance Prevention:** Daemon mode uses `/var/run/*.pid` file locking on POSIX platforms and global named Mutex (`CreateMutexW`) on Windows. Standalone mode bypasses this restriction for development convenience.

## 5. Logging Architecture

The logging architecture routes through an abstract interface operating asynchronously via a **ring buffer** to prevent performance degradation. The output sink is dynamically chosen based on execution mode:

* **Standalone Mode Logging:** Logs do not go to files or syslog, outputting strictly to **standard output/error (`std::cout`, `std::cerr`) console**. Uses structured string formatting with timestamps and log levels.
* **Daemon/Service Mode Logging:** 
  * **System Native Logger:** Sent to Windows `Event Log` or POSIX `syslog`.
  * **File Logger:** Recorded asynchronously to specified paths using `std::filesystem::path` and `std::ofstream`. Features an automated log rotation (`rotate_log_files()`) mechanism upon exceeding file size limits or date changes.

## 6. Implementation Checklist

- [ ] Are all functions, variables, and class names written in `snake_case` according to C++ standard styles?
- [ ] Does the source directory structure strictly adhere to `include/`, `src/`, `src/{linux,win32,darwin,bsd}` rules?
- [ ] Is transition to Standalone mode smooth depending on command-line options?
- [ ] In Standalone mode, are files avoided and logs immediately written to console?
- [ ] Is OS-specific implementation code completely isolated inside platform folders without `#ifdef` branching?
- [ ] In Daemon mode, does the file logger perform rotation when exceeding specified file sizes?
- [ ] Upon shutdown requests, are threads and sockets cleanly disposed without leaks across all platforms?
