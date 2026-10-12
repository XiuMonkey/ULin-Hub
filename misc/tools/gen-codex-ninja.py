#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen-codex-ninja.py —— 不用 cmake，直接生成 CodexBackend 扩展的 ninja 工程。

为什么需要它：本机环境里 `cmake -S ... -B ... -G Ninja` 在**编译器探测阶段就挂死**
（cmake/ninja/cl 单独都能跑；ninja + cl 手工驱动也验证过能出 .obj —— 见 build/temp/ninja-probe）。
宿主用 MSBuild 编得好好的，所以问题在 cmake 自身，而不在工具链。

这个生成器复刻 extensions/CodexBackend/CMakeLists.txt 的编译/链接定义：
  · 源文件表         ← CMakeLists 的 qt_add_plugin(...) 参数
  · include 路径     ← target_include_directories
  · 链接库           ← target_link_libraries（Core / Network / Widgets）
  · 编译选项         ← /utf-8 + Qt6 插件需要的预处理器定义（Q_PLUGIN_METADATA 依赖 QT_PLUGIN）
  · moc              ← 对含 Q_OBJECT 的头逐个跑 moc（Qt 自带 moc.exe）

用法：
  python misc/tools/gen-codex-ninja.py [--build-dir <dir>]
  D:\\Qt\\Tools\\Ninja\\ninja.exe -C <dir>
"""

import argparse
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXT = os.path.join(REPO, "extensions", "CodexBackend")
HOST_INCLUDE = os.path.join(REPO, "source", "include")
QT = r"D:\Qt\6.11.2\msvc2022_64"
MOC = os.path.join(QT, "bin", "moc.exe")

# CMakeLists 的 qt_add_plugin 源文件表（cpp 是要编的；h 只进 IDE，moc 另行判定）
CPP_FILES = [
    "CodexBackendPlugin.cpp",
    "CodexAppServer.cpp",
    "CodexPluginMarket.cpp",
    "CodexMarketWindow.cpp",
    "CodexSettingsWindow.cpp",
    "CodexSettingsPagesAccount.cpp",
    "CodexSettingsPagesModel.cpp",
    "CodexSettingsPagesPolicy.cpp",
    "CodexSettingsPagesExtras.cpp",
    "CodexConfigApi.cpp",
    "CodexBackendSettings.cpp",
    "DshHostBridge.cpp",
    "CodexPanelApi.cpp",
]

HEADER_FILES = [
    "CodexBackendPlugin.h",
    "CodexAppServer.h",
    "CodexApprovalMap.h",
    "CodexHistoryMap.h",
    "CodexPluginMarket.h",
    "CodexMarketWindow.h",
    "CodexSettingsWindow.h",
    "CodexSettingsUi.h",
    "CodexLog.h",
    "CodexConfigApi.h",
    "CodexBackendSettings.h",
    "DshHostBridge.h",
    "CodexPanelApi.h",
]

QT_INCLUDES = ["QtCore", "QtGui", "QtWidgets", "QtNetwork", "QtQml", "QtQuick"]

# qt_add_plugin / Qt6 的常规定义。QT_PLUGIN 必须有：Q_PLUGIN_METADATA 展开出的
# qt_plugin_instance / qt_plugin_query_metadata 两个 C 入口只在定义了它时才生成，
# 少了宿主 QPluginLoader 会报"不是有效的 Qt 插件"。
DEFINES = [
    "QT_PLUGIN",
    "QT_NO_DEBUG",
    "QT_CORE_LIB",
    "QT_GUI_LIB",
    "QT_WIDGETS_LIB",
    "QT_NETWORK_LIB",
    "QT_QML_LIB",
    "QT_QUICK_LIB",
    "UNICODE",
    "_UNICODE",
    "WIN32",
    "_WINDOWS",
    "NDEBUG",
]

# Core / Gui / Widgets / Network（与 target_link_libraries 一致；Gui 是 Widgets 的依赖）
QT_LIBS = ["Qt6Core", "Qt6Gui", "Qt6Widgets", "Qt6Network"]
SYSTEM_LIBS = ["kernel32", "user32", "shell32", "ole32", "advapi32", "ws2_32", "version", "uuid"]


def has_qobject(path):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    except OSError:
        return False
    return re.search(r"\bQ_OBJECT\b", text) is not None or "Q_PLUGIN_METADATA" in text


# 引号形式 #include 的解析根：先源文件同目录，再扩展目录，再宿主 include。
INCLUDE_ROOTS = [EXT, HOST_INCLUDE]


def scan_headers(entry, roots=None, seen=None):
    """递归收集 entry 里 `#include "..."` 能解析到的**仓库内**头文件。

    为什么需要它：这个 build.ninja 里 `deps = msvc` 是**空转**的 —— cc 规则没接
    `/showIncludes`，于是 ninja 的 .ninja_deps 里每个对象都是 `#deps 0`（实测），
    **改头文件不会触发重编** ⇒ 会静默用着陈旧 obj 链出 DLL（最危险的是 VirtualCommon.h，
    它被 CodexBackendPlugin.cpp / DshHostBridge.cpp 包含）。
    改成由生成器把依赖**显式**写成 ninja 的 implicit 输入：与 cl 的消息语言/locale 无关，
    比 `msvc_deps_prefix` 那套稳（本机 cl 是中文，前缀要靠猜）。
    只跟引号形式；`<QtCore/...>` 这类尖括号的系统头不动（换 Qt 时本来就该全清重编）。
    """
    if roots is None:
        roots = INCLUDE_ROOTS
    if seen is None:
        seen = set()
    out = []
    try:
        with open(entry, "r", encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    except OSError:
        return out
    for m in re.finditer(r'^\s*#\s*include\s+"([^"]+)"', text, re.M):
        rel = m.group(1)
        for root in [os.path.dirname(entry)] + list(roots):
            cand = os.path.normpath(os.path.join(root, rel))
            if os.path.isfile(cand):
                if cand in seen:
                    break
                seen.add(cand)
                out.append(cand)
                out.extend(scan_headers(cand, roots, seen))
                break
    return out


def win(path):
    return path.replace("/", "\\")


def npath(path):
    """ninja 语法里的路径。含空格时转义规则很挑（实测）：

      · **输出**用相对路径（相对于 build.ninja 所在目录）⇒ 空格直接写，只把冒号写成 `$:`。
      · **输入**必须是绝对路径（ninja 在 build 目录下运行）⇒ 先转义空格为 `$ `，
        再把盘符的 `: ` 变成 `$: `；顺序不能反。
    """
    p = win(path)
    if os.path.isabs(path):
        p = p.replace(" ", "$ ").replace(": ", "$: ")
    else:
        p = p.replace(":", "$:")
    return p


def rel(path, base):
    return os.path.relpath(path, base)


def npath2(path, base):
    """输出路径与命令行里的路径：优先用**相对** build 目录的路径（那样没有盘符冒号，
    只需要转义空格），实在不行才用绝对路径 + 内联转义 `C$:\\`。"""
    try:
        r = os.path.relpath(path, base)
    except ValueError:
        r = path
    if not r.startswith(".."):
        return win(r).replace(" ", "$ ")
    return win(path).replace(" ", "$ ").replace(":", "$:")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", default=os.path.join(EXT, "build-ninja"))
    args = ap.parse_args()
    build_dir = os.path.abspath(args.build_dir)
    os.makedirs(build_dir, exist_ok=True)

    cpp_abs = [os.path.join(EXT, name) for name in CPP_FILES]
    missing = [p for p in cpp_abs if not os.path.isfile(p)]
    if missing:
        print("缺少源文件:", *missing, sep="\n  ")
        return 2

    moc_headers = [os.path.join(EXT, n) for n in HEADER_FILES
                   if os.path.isfile(os.path.join(EXT, n)) and has_qobject(os.path.join(EXT, n))]

    includes = " ".join(
        [f'/I "{win(HOST_INCLUDE)}"', f'/I "{win(os.path.join(QT, "include"))}"']
        + [f'/I "{win(os.path.join(QT, "include", m))}"' for m in QT_INCLUDES])
    defines = " ".join(f"/D{d}" for d in DEFINES)
    # /utf-8：源文件是 UTF-8 无 BOM 且含中文注释；不指定 MSVC 可能按系统代码页读（C4819/C1019）
    cflags = f"/nologo /c /EHsc /std:c++20 /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8 /W3 /MD {defines} {includes}"

    dll = os.path.join(build_dir, "codex_backend.dll")
    obj_files = []
    lines = []
    lines.append("# 由 misc/tools/gen-codex-ninja.py 生成 —— 手改无效（改生成器）")
    lines.append("ninja_required_version = 1.5")
    lines.append("")
    lines.append("msvc = C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.51.36231")
    lines.append(f"moc = {win(MOC)}")
    lines.append("")
    lines.append(f"cflags = {cflags}")
    lines.append("")
    lines.append("rule moc")
    # moc 也要 -I 宿主 include：Q_INTERFACES(DshHostPlugin VirtualApiSink) 里的接口定义在宿主头里，
    # 不给路径 moc 会报 "Undefined interface"（实测 CodexBackendPlugin.h(55:1)）。
    mocinc = f'-I "{win(HOST_INCLUDE)}"'
    lines.append(f"  command = $moc $in -o $out {mocinc}")
    lines.append("  description = MOC $out")
    lines.append("")
    lines.append("rule cc")
    lines.append(f"  command = cl @$out.rsp")
    lines.append("  description = CC $out")
    # 不写 deps = msvc：cc 规则没接 /showIncludes，写了也是空转（.ninja_deps 里全是 #deps 0），
    # 反而让人以为头文件依赖被跟踪。头文件依赖改由生成器显式写成 implicit 输入（见 scan_headers）。
    lines.append("  rspfile = $out.rsp")
    lines.append("  rspfile_content = $cflags /Fo$out $in")
    lines.append("")
    lines.append("rule link")
    # 不用 rspfile：ninja 的变量不递归展开，把 $out 写进 rspfile_content 变量里，
    # 引用时不会展开 ⇒ link 收到空的 /OUT:（实测 LNK1146）。直接把命令行写全最稳。
    lines.append("  command = link /NOLOGO /DLL /INCREMENTAL:NO /OUT:$out $in $linkflags")
    lines.append("  description = LINK $out")

    moc_objs = []
    moc_hdrs = []
    for h in moc_headers:
        base = os.path.splitext(os.path.basename(h))[0]
        out = os.path.join(build_dir, f"moc_{base}.cpp")
        lines.append("")
        lines.append(f"build {npath2(out, build_dir)}: moc {npath2(h, build_dir)}")
        obj = os.path.join(build_dir, f"moc_{base}.obj")
        moc_objs.append(obj)
        moc_hdrs.append(h)

    for src in cpp_abs:
        base = os.path.splitext(os.path.basename(src))[0]
        obj = os.path.join(build_dir, f"{base}.obj")
        obj_files.append(obj)
        hdrs = scan_headers(src)
        implicit = "".join(f" {npath2(h, build_dir)}" for h in hdrs)
        lines.append("")
        lines.append(f"build {npath2(obj, build_dir)}: cc {npath2(src, build_dir)}"
                     + (f" |{implicit}" if hdrs else ""))

    for obj, hdr in zip(moc_objs, moc_hdrs):
        obj_files.append(obj)
        # moc 产物 include 的是原头；原头自己的 #include 变了也得重编 ⇒ 把它的传递依赖一起写上
        hdrs = [hdr] + scan_headers(hdr)
        implicit = "".join(f" {npath2(h, build_dir)}" for h in hdrs)
        lines.append("")
        lines.append(f"build {npath2(obj, build_dir)}: cc {npath2(obj[:-4] + '.cpp', build_dir)}"
                     + (f" |{implicit}" if hdrs else ""))

    # 链接：/LIBPATH + 各库
    lines.append("")
    libpaths = " ".join([f'/LIBPATH:"{win(os.path.join(QT, "lib"))}"'])
    libs = " ".join(f"{l}.lib" for l in QT_LIBS + SYSTEM_LIBS)
    lines.append(f"linkflags = {libpaths} {libs}")
    lines.append("")
    lines.append(f"build {npath2(dll, build_dir)}: link " + " ".join(npath2(o, build_dir) for o in obj_files))
    lines.append("")
    lines.append("default " + npath2(dll, build_dir))
    lines.append("")

    ninja_path = os.path.join(build_dir, "build.ninja")
    # ninja 只接受 UTF-8 且**不能带 BOM**（带 BOM 会 lexing error —— 实测过）。
    # 用 utf-8-sig 之外的方式：显式 utf-8 + newline="\n"。
    with open(ninja_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines))

    print(f"生成: {ninja_path}")
    print(f"  cpp: {len(cpp_abs)} 个    moc: {len(moc_headers)} 个    obj: {len(obj_files)} 个")
    print(f"  moc 头: {', '.join(os.path.basename(h) for h in moc_headers)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
