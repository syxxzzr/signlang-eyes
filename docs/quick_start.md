# Quick Start

[简体中文](quick_start.md) | [English](quick_start.en.md)

本文说明如何在开发主机上为 `aarch64` 目标平台准备依赖、配置工程并完成安装。

## 前置条件

- Conan 2.x
- CMake 3.20+
- GCC 11+，并确保 `aarch64-linux-gnu-gcc` 和 `aarch64-linux-gnu-g++` 在 `PATH` 中
- 目标平台 sysroot，默认路径为 `/root/sysroot`

本项目仅支持 `aarch64` / `arm64` / `armv8` 目标平台，且仅对 `RK3588` 硬件平台做了适配。以下命令默认在仓库根目录执行。

如果 sysroot 不在默认位置，可以在安装依赖前指定：

```bash
export SIGNLANG_AARCH64_SYSROOT=/path/to/aarch64/sysroot
```

## 准备 Conan

如需把 Conan 缓存隔离到当前仓库，可设置 `CONAN_HOME`：

```bash
export CONAN_HOME="$PWD/.conan"
```

生成默认构建机 profile：

```bash
conan profile detect --force
```

导入本仓库维护的 Conan recipe：

```bash
conan export conan/recipes/cpp-pinyin
conan export conan/recipes/iceoryx2
conan export conan/recipes/librga
conan export conan/recipes/rknn-runtime
```

## 安装第三方依赖

```bash
conan install . \
  -of build-aarch64 \
  -pr:h conan/profiles/linux-aarch64-gcc \
  -pr:b default \
  --build=missing
```

如果在 WSL 或内存较小的机器上构建，可以限制 Conan 构建并行度：

```bash
conan install . \
  -of build-aarch64 \
  -pr:h conan/profiles/linux-aarch64-gcc \
  -pr:b default \
  --build=missing \
  -c:b tools.build:jobs=4 \
  -c:h tools.build:jobs=4
```

## 配置、构建和安装

```bash
cmake -S . -B build-aarch64 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=build-aarch64/conan_toolchain.cmake \
  -DCMAKE_INSTALL_PREFIX=install

cmake --build build-aarch64 --target install -j"$(nproc)"
```

如果构建机器资源有限，可以降低 CMake 构建并行度：

```bash
cmake --build build-aarch64 --target install -j4
```

安装完成后，主程序位于：

```bash
install/launcher
```
