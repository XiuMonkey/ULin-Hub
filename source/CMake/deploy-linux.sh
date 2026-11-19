#!/usr/bin/env bash
# ULin Hub —— Linux (Ubuntu) 一键部署
# 用法：curl -fsSL http://192.168.80.1:8000/deploy.sh | bash
set -euo pipefail

HOST="http://192.168.80.1:8000"
BASE="${ULINHUB_HOME:-$HOME/ulinhub-linux}"
TAR="$BASE/ulinhub-linux.tar.gz"

echo "==> 工作目录：$BASE"
mkdir -p "$BASE"
cd "$BASE"

echo "==> [1/5] 下载源码包（约 10 MB）"
if [ ! -f "$TAR" ]; then
    curl -fL --retry 3 --connect-timeout 10 "$HOST/ulinhub-linux.tar.gz" -o "$TAR"
fi
tar xzf "$TAR"

echo "==> [2/5] 安装编译依赖（需要 sudo，会提示输入密码）"
PKGS="build-essential cmake ninja-build pkg-config xz-utils \
      qt6-base-dev qt6-base-private-dev qt6-declarative-dev \
      qt6-webengine-dev qt6-webchannel-dev qt6-websockets-dev \
      libgl1-mesa-dev libxkbcommon-dev libegl1-mesa-dev \
      libxcb-cursor0 libxcb-xinerama0 libxkbcommon-x11-0 libgl1 libegl1"
sudo apt-get update -qq
sudo apt-get install -y $PKGS

echo "==> [3/5] 编译 release（CMake/Ninja）"
bash source/CMake/build-linux.sh release

OUT="$BASE/x64/Linux-x64/release"
echo
echo "================================================"
echo " 构建完成：$OUT/ULin Hub"
echo " 运行时：  $OUT/resources/{server,node}"
echo " 启动：    cd \"$OUT\" && ./run.sh"
echo " 说明：    release 由 CMake POST_BUILD 同步服务端/Node；debug 不配置"
echo "================================================"
