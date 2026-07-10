# Quick Start

[简体中文](quick_start.md) | [English](quick_start.en.md)

This guide covers how to prepare dependencies, configure the project, and produce an install image for the `aarch64` target platform on a development host.

## Prerequisites

- Conan 2.x
- CMake 3.20+
- GCC 11+, with `aarch64-linux-gnu-gcc` and `aarch64-linux-gnu-g++` on `PATH`
- Target platform sysroot; default location is `/root/sysroot`

This project supports `aarch64` / `arm64` / `armv8` targets only and has been validated exclusively on the `RK3588` hardware platform. All commands below are assumed to run from the repository root.

If your sysroot is not at the default path, export the following before installing dependencies:

```bash
export SIGNLANG_AARCH64_SYSROOT=/path/to/aarch64/sysroot
```

## Prepare Conan

To isolate the Conan cache within the repository, set `CONAN_HOME`:

```bash
export CONAN_HOME="$PWD/.conan"
```

Generate the default build-machine profile:

```bash
conan profile detect --force
```

Export the Conan recipes maintained in this repository:

```bash
conan export conan/recipes/cpp-pinyin
conan export conan/recipes/iceoryx2
conan export conan/recipes/librga
conan export conan/recipes/rknn-runtime
```

## Install Third-Party Dependencies

```bash
conan install . \
  -of build-aarch64 \
  -pr:h conan/profiles/linux-aarch64-gcc \
  -pr:b default \
  --build=missing
```

When building inside WSL or on a machine with limited memory, constrain the Conan build parallelism:

```bash
conan install . \
  -of build-aarch64 \
  -pr:h conan/profiles/linux-aarch64-gcc \
  -pr:b default \
  --build=missing \
  -c:b tools.build:jobs=4 \
  -c:h tools.build:jobs=4
```

## Configure, Build, and Install

```bash
cmake -S . -B build-aarch64 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=build-aarch64/conan_toolchain.cmake \
  -DCMAKE_INSTALL_PREFIX=install

cmake --build build-aarch64 --target install -j"$(nproc)"
```

On resource-constrained build machines, lower the CMake build parallelism:

```bash
cmake --build build-aarch64 --target install -j4
```

After installation, the main entry point is located at:

```bash
install/launcher
```
