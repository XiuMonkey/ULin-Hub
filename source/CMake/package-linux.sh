#!/usr/bin/env bash
# 把系统 Qt / QML / WebEngine 运行库收进 release 输出目录。
# 目标：输出目录不再依赖目标机预装 Qt 6.10.2；由 run.sh 设置 LD_LIBRARY_PATH /
# QT_PLUGIN_PATH / QML2_IMPORT_PATH / QTWEBENGINE_*，直接运行。
#
# 用法：package-linux.sh <releaseDir>
#   stage-runtime.sh 负责 server/node/resources；本脚本负责 Qt 运行库。
set -euo pipefail

APP_DIR="${1:-}"
if [ -z "$APP_DIR" ] || [ ! -x "$APP_DIR/ULin Hub" ]; then
	echo "用法: package-linux.sh <releaseDir>（目录里要有可执行的 ULin Hub）" >&2
	exit 1
fi

QT_LIBS="$(qtpaths6 --query QT_INSTALL_LIBS)"
QT_QML="$(qtpaths6 --query QT_INSTALL_QML)"
QT_PLUGINS="$(qtpaths6 --query QT_INSTALL_PLUGINS)"
QT_LIBEXECS="$(qtpaths6 --query QT_INSTALL_LIBEXECS)"
QT_DATA="$(qtpaths6 --query QT_INSTALL_DATA)"
QT_TRANSLATIONS="$(qtpaths6 --query QT_INSTALL_TRANSLATIONS)"

# 编译产物/链接阶段会由 stage-runtime 删除此 stamp；没有新链接时直接复用已打包的 Qt。
PKG_STAMP="$APP_DIR/.package-linux.stamp"
if [ -f "$PKG_STAMP" ] \
	&& [ -x "$APP_DIR/libexec/QtWebEngineProcess" ] \
	&& [ -d "$APP_DIR/lib" ] && [ -d "$APP_DIR/qml" ] \
	&& [ -d "$APP_DIR/plugins" ] && [ -d "$APP_DIR/resources/webengine" ]; then
	echo "==> Qt 运行库未变化，跳过重复打包"
	exit 0
fi

echo "==> Qt libs: $QT_LIBS"
echo "==> Qt QML:  $QT_QML"
echo "==> Qt plugins: $QT_PLUGINS"

rm -rf "$APP_DIR/lib" "$APP_DIR/qml" "$APP_DIR/plugins" \
	"$APP_DIR/libexec" "$APP_DIR/resources/webengine"
mkdir -p "$APP_DIR/lib" "$APP_DIR/libexec" "$APP_DIR/resources/webengine"

# QML / 插件必须展开 symlink；共享目录复制到输出后再也不依赖系统路径。
echo "==> 复制 Qt QML 模块"
cp -aL "$QT_QML/." "$APP_DIR/qml/"
echo "==> 复制 Qt 插件"
cp -aL "$QT_PLUGINS/." "$APP_DIR/plugins/"

if [ -f "$QT_LIBEXECS/QtWebEngineProcess" ]; then
	echo "==> 复制 QtWebEngineProcess"
	cp -a "$QT_LIBEXECS/QtWebEngineProcess" "$APP_DIR/libexec/"
	chmod +x "$APP_DIR/libexec/QtWebEngineProcess"
fi

if [ -d "$QT_DATA/resources" ]; then
	echo "==> 复制 WebEngine 资源"
	cp -a "$QT_DATA/resources/." "$APP_DIR/resources/webengine/"
fi
if [ -d "$QT_TRANSLATIONS/qtwebengine_locales" ]; then
	echo "==> 复制 WebEngine locales"
	cp -a "$QT_TRANSLATIONS/qtwebengine_locales" "$APP_DIR/resources/webengine/locales"
fi

# 递归 ldd：只收 /usr/lib 的依赖，/lib 下的 glibc/ld-linux 留给系统。
declare -A seen
copy_deps() {
	local file="$1"
	[ -f "$file" ] || return 0
	while IFS= read -r line; do
		local dep
		dep="$(awk '{ for (i = 1; i <= NF; ++i) if ($i ~ /^\//) { print $i; exit } }' <<< "$line")"
		[ -n "$dep" ] || continue
		case "$dep" in
			/lib/*|/lib64/*) continue ;;
		esac
		local base
		base="$(basename "$dep")"
		if [ -z "${seen[$base]:-}" ]; then
			seen[$base]=1
			cp -aL "$dep" "$APP_DIR/lib/$base"
			copy_deps "$APP_DIR/lib/$base"
		fi
	done < <(ldd "$file" 2>/dev/null || true)
}

echo "==> 收集 Qt/WebEngine 依赖"
copy_deps "$APP_DIR/ULin Hub"
while IFS= read -r -d '' so; do
	copy_deps "$so"
done < <(find "$APP_DIR/qml" "$APP_DIR/plugins" -type f -name '*.so*' -print0)
if [ -f "$APP_DIR/libexec/QtWebEngineProcess" ]; then
	copy_deps "$APP_DIR/libexec/QtWebEngineProcess"
fi

cat > "$APP_DIR/qt.conf" <<'EOF'
[Paths]
Prefix = .
Libraries = lib
Plugins = plugins
Qml2Imports = qml
Data = .
Translations = resources/translations
EOF

# run.sh 最终由这里重写：stage 生成的版本只负责 server/resources，这里再补 Qt 环境。
cat > "$APP_DIR/run.sh" <<'RUN'
#!/usr/bin/env bash
cd "$(dirname "$0")"
APPDIR="$PWD"

# 本机凭据：DEEPSEEK_API_KEY=sk-...（chmod 600），不要写进仓库。
if [ -f "${XDG_CONFIG_HOME:-$HOME/.config}/ulinhub/env" ]; then
    set -a
    . "${XDG_CONFIG_HOME:-$HOME/.config}/ulinhub/env"
    set +a
fi

export LD_LIBRARY_PATH="$APPDIR/lib:${LD_LIBRARY_PATH:-}"
export QT_PLUGIN_PATH="$APPDIR/plugins"
export QML2_IMPORT_PATH="$APPDIR/qml"
export QML_IMPORT_PATH="$APPDIR/qml"
export QTWEBENGINE_RESOURCES_PATH="$APPDIR/resources/webengine"
export QTWEBENGINE_LOCALES_PATH="$APPDIR/resources/webengine/locales"
if [ -x "$APPDIR/libexec/QtWebEngineProcess" ]; then
    export QTWEBENGINEPROCESS_PATH="$APPDIR/libexec/QtWebEngineProcess"
fi
export LIBGL_ALWAYS_SOFTWARE="${LIBGL_ALWAYS_SOFTWARE:-1}"
export QTWEBENGINE_DISABLE_SANDBOX=1
export QTWEBENGINE_CHROMIUM_FLAGS="--disable-gpu --no-sandbox ${QTWEBENGINE_CHROMIUM_FLAGS:-}"
exec "$APPDIR/ULin Hub" "$@"
RUN
chmod +x "$APP_DIR/run.sh"

echo "==> 运行库打包完成：$APP_DIR"
touch "$PKG_STAMP"
echo "    lib=$(find "$APP_DIR/lib" -type f | wc -l) files, qml=$(du -sh "$APP_DIR/qml" | cut -f1), plugins=$(du -sh "$APP_DIR/plugins" | cut -f1)"
