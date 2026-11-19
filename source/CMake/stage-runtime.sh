#!/usr/bin/env bash
# 把 Linux 内置服务端运行时装进 release 输出目录：
#   <out>/resources/server/       storage/server + Linux 原生 node_modules
#   <out>/resources/node/         assets/resources/node（harness-node-entry.mjs）
#   <out>/run.sh                  带 WebEngine/VMware 兜底的启动脚本
#
# 用法：
#   stage-runtime.sh [outDir]
#   outDir 默认 <repo>/x64/Linux-x64/release
#
# 为什么先装到本地 build/linux-runtime-server：
#   pnpm 在 VMware 共享目录（HGFS）上既不能建 symlink，也经常 rename EACCES。
#   所以先在客户机本地磁盘把 Linux node_modules 装好，再用 cp -aL 展开到共享输出目录。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP_DIR="${1:-$ROOT/x64/Linux-x64/release}"
NODE_VERSION="${NODE_VERSION:-v24.9.0}"
SERVER_SRC="$ROOT/assets/storage/server"
RUNTIME_CACHE="${ULINHUB_RUNTIME_CACHE:-/var/tmp/ulinhub-runtime-server}"

case "$(uname -m)" in
	x86_64|amd64) NODE_ARCH="x64" ;;
	aarch64|arm64) NODE_ARCH="arm64" ;;
	*) echo "不支持的架构: $(uname -m)" >&2; exit 1 ;;
esac

if [ ! -f "$SERVER_SRC/node_modules/@deepseek-ai/dsh/lib/bin.js" ]; then
	echo "缺少 DSH 运行时：$SERVER_SRC/node_modules/@deepseek-ai/dsh/lib/bin.js" >&2
	echo "$SERVER_SRC 需要包含 node_modules；它不再放在 assets/resources 下。" >&2
	exit 1
fi

# Linux node 放在 storage/server/node；缺失时下载到同一个位置。
if [ ! -x "$SERVER_SRC/node" ]; then
	echo "==> 下载 Node ${NODE_VERSION} (${NODE_ARCH}) 到 storage/server/node"
	tmp="$(mktemp -d)"
	trap 'rm -rf "$tmp"' EXIT
	tar_name="node-${NODE_VERSION}-linux-${NODE_ARCH}.tar.xz"
	tar_path="$tmp/$tar_name"

	curl -fL --retry 3 --connect-timeout 15 \
		"https://nodejs.org/dist/${NODE_VERSION}/${tar_name}" -o "$tar_path"
	curl -fsSL "https://nodejs.org/dist/${NODE_VERSION}/SHASUMS256.txt" -o "$tmp/SHASUMS256.txt"
	(cd "$tmp" && grep " ${tar_name}$" SHASUMS256.txt | sha256sum -c -)

	tar -xJf "$tar_path" -C "$tmp"
	install -m 0755 "$tmp/node-${NODE_VERSION}-linux-${NODE_ARCH}/bin/node" "$SERVER_SRC/node"
else
	echo "==> 使用 storage/server/node"
fi

chmod +x "$SERVER_SRC/node"
"$SERVER_SRC/node" --version

# 在客户机本地磁盘准备带 Linux 原生依赖的 server 副本。
if [ "${ULINHUB_REFRESH_RUNTIME:-0}" = "1" ]; then
	rm -rf "$RUNTIME_CACHE"
fi
if [ ! -f "$RUNTIME_CACHE/.ulinhub-linux-deps" ]; then
	echo "==> 准备本地运行环境：$RUNTIME_CACHE"
	rm -rf "$RUNTIME_CACHE"
	mkdir -p "$(dirname "$RUNTIME_CACHE")"
	cp -a "$SERVER_SRC" "$RUNTIME_CACHE"
	rm -f "$RUNTIME_CACHE/node.exe"

	if [ "${ULINHUB_SKIP_NODE_DEPS:-0}" != "1" ] \
		&& [ -f "$RUNTIME_CACHE/node_modules/pnpm/artifacts/exe/dist/pnpm.mjs" ]; then
		echo "==> 安装 Linux 原生依赖（首次约几分钟）"
		(cd "$RUNTIME_CACHE" && ./node node_modules/pnpm/artifacts/exe/dist/pnpm.mjs \
			install --prod --force --reporter=append-only \
			--config.node-linker=hoisted \
			--config.package-import-method=copy) || \
			echo "警告：pnpm install 返回非 0，继续校验 DSH 是否可用"
	fi

	if [ -d "$RUNTIME_CACHE/node_modules/@deepseek-ai/dsh-app-boot" ]; then
		touch "$RUNTIME_CACHE/.ulinhub-linux-deps"
	fi
fi

mkdir -p "$APP_DIR/resources"
RES="$APP_DIR/resources"

echo "==> 展开运行环境 -> $RES/server"
rm -rf "$RES/server"
mkdir -p "$RES/server"
# 复制到 HGFS 时不能带 -a：-a 会保留 symlink/硬链接，HGFS 既不支持 symlink，
# 也不允许 cp 为 .bin 建硬链接。用 -rL 跟随 symlink 成实体文件，只保留 mode/timestamp。
cp -rL --preserve=mode,timestamps "$RUNTIME_CACHE/." "$RES/server/"
rm -f "$RES/server/node.exe"

echo "==> 复制 assets/resources/node -> $RES/node"
rm -rf "$RES/node"
cp -a "$ROOT/assets/resources/node" "$RES/node"

if [ -f "$ROOT/assets/resources/webengine/qtwebengine_resources.pak" ]; then
	echo "==> 复制 WebEngine 资源"
	rm -rf "$RES/webengine"
	cp -a "$ROOT/assets/resources/webengine" "$RES/webengine"
fi

chmod +x "$RES/server/node"

cat > "$APP_DIR/run.sh" <<'RUN'
#!/usr/bin/env bash
cd "$(dirname "$0")"
# 本机凭据：不要把 DEEPSEEK_API_KEY 写进仓库或命令行历史。
# 文件格式（chmod 600）：
#   DEEPSEEK_API_KEY=sk-...
if [ -f "${XDG_CONFIG_HOME:-$HOME/.config}/ulinhub/env" ]; then
    set -a
    . "${XDG_CONFIG_HOME:-$HOME/.config}/ulinhub/env"
    set +a
fi
export LIBGL_ALWAYS_SOFTWARE="${LIBGL_ALWAYS_SOFTWARE:-1}"
export QTWEBENGINE_DISABLE_SANDBOX=1
export QTWEBENGINE_CHROMIUM_FLAGS="--disable-gpu --no-sandbox ${QTWEBENGINE_CHROMIUM_FLAGS:-}"
exec "./ULin Hub" "$@"
RUN
chmod +x "$APP_DIR/run.sh"

echo "==> release 运行环境已就绪：$APP_DIR"
echo "    启动：cd \"$APP_DIR\" && ./run.sh"
echo "    强制重装原生依赖：ULINHUB_REFRESH_RUNTIME=1 bash source/CMake/stage-runtime.sh <outDir>"
