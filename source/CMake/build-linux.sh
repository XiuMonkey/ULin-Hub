#!/usr/bin/env bash
# Linux 构建入口（CMake/Ninja）。
# 产物：<repo>/x64/Linux-x64/release/ULin Hub
#       <repo>/x64/Linux-x64/debug/ULin Hub
# Release 构建完成后自动执行 stage-runtime.sh，把 storage/server 和
# assets/resources/node 同步到 release 输出目录；Debug 不配置运行时。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CONFIG="${1:-release}"
case "$CONFIG" in
	release|Release) CMAKE_CONFIG="Release"; OUT="$ROOT/x64/Linux-x64/release" ;;
	debug|Debug) CMAKE_CONFIG="Debug"; OUT="$ROOT/x64/Linux-x64/debug" ;;
	*) echo "用法: build-linux.sh [release|debug]" >&2; exit 1 ;;
esac

# 默认统一使用 build/cmake；Debug/Release 共用同一棵 CMake 构建树，
# 切换配置时 CMake 会重新生成。需要两套隔离时设 ULINHUB_BUILD_DIR。
BUILD_DIR="${ULINHUB_BUILD_DIR:-$ROOT/build/cmake}"
mkdir -p "$OUT"

echo "==> 配置 $CMAKE_CONFIG -> $BUILD_DIR"
cmake -S "$ROOT/source/CMake" -B "$BUILD_DIR" -G Ninja \
	-DCMAKE_BUILD_TYPE="$CMAKE_CONFIG" \
	-DULINHUB_BUILD_TESTS=OFF

echo "==> 编译"
cmake --build "$BUILD_DIR" -j"$(nproc)"

if [ "$CMAKE_CONFIG" = "Release" ]; then
	echo "==> 打包 Qt / QML / WebEngine 运行库"
	bash "$ROOT/source/CMake/package-linux.sh" "$OUT"
else
	echo "==> Debug 不配置服务端/Node 运行环境"
fi

echo "==> 完成：$OUT/ULin Hub"
