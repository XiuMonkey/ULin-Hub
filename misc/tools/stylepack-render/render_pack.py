"""把一个风格包叠加到内置 QML 树上并渲染一张预览图。

用法：python render_pack.py <packDir> <light|dark> <outPng>
中间产物落在本目录的 _work/（ui 树 + 生成的 Theme.qml），不污染源码树。

原理：真实客户端里 Theme/Style/WindowBridge/HostUi/Hub 是 C++ 注册的单例；这里用
`ULinHub/` 下的 QML 单例替代（Theme.qml 由 gen_theme.py 按色板 + metrics 生成），
再把 source/qml 整棵树复制成 ui/、叠加风格包覆盖件，最后用 qml.exe 渲染 Preview.qml。
注意：色板颜色要先用 #AARRGGBB 形式展开（QML 的 color 类型不吃 `rgba(...)` 函数写法）。
"""
import html
import os
import re
import shutil
import subprocess
import sys

HARNESS = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HARNESS)))  # .../ULin Hub
QML_SRC = os.path.join(REPO, "source", "qml")
STYLES = os.path.join(REPO, "assets", "resources", "styles")
TS_FILE = os.path.join(REPO, "assets", "translations", "ulinhub_zh_CN.ts")
UI = os.path.join(HARNESS, "_work", "ui")
PY = sys.executable
QML_EXE = os.environ.get("QML_EXE", r"D:\Qt\6.11.2\msvc2022_64\bin\qml.exe")


def load_translations():
    """从宿主的 .ts 里读 id -> 中文。qml.exe 不会加载 .qm，所以预览里所有
    qsTrId("…") 都会原样显示成 key（之前预览图上真出现过 sessionBar_shortcut_search
    这种字面量）。这里在叠完 ui 树之后把调用点替换成字面量，预览才跟真实界面一致。"""
    if not os.path.isfile(TS_FILE):
        return {}
    text = open(TS_FILE, encoding="utf-8").read()
    table = {}
    for m in re.finditer(r'<message id="([^"]+)"[^>]*>(.*?)</message>', text, re.S):
        key, body = m.group(1), m.group(2)
        t = re.search(r"<translation[^>]*>(.*?)</translation>", body, re.S)
        if not t:
            continue
        value = html.unescape(t.group(1)).strip()
        if value:                      # 空 <translation/> = 未翻译，保持原样
            table[key] = value
    return table


def localize_ui(table):
    """把 qsTrId("key") 换成 "译文"。只动单/双引号字符串，不碰注释里的示例。"""
    if not table:
        return 0
    pat = re.compile(r'qsTrId\(\s*"([^"]+)"\s*(?:,[^)]*)?\)')
    pat_s = re.compile(r"qsTrId\(\s*'([^']+)'\s*(?:,[^)]*)?\)")
    hits = 0
    for base, _dirs, files in os.walk(UI):
        for f in files:
            if not f.endswith(".qml"):
                continue
            path = os.path.join(base, f)
            src = open(path, encoding="utf-8").read()
            before = src

            def repl(m, quote='"'):
                key = m.group(1)
                value = table.get(key)
                if value is None:
                    return m.group(0)
                return quote + value.replace("\\", "\\\\").replace('"', '\\"') + quote

            src = pat.sub(lambda m: repl(m, '"'), src)
            src = pat_s.sub(lambda m: repl(m, "'"), src)
            if src != before:
                hits += 1
                open(path, "w", encoding="utf-8", newline="\n").write(src)
    return hits


def rebuild_ui(pack, mode):
    if os.path.isdir(UI):
        shutil.rmtree(UI)
    os.makedirs(UI)
    for part in ("basic", "skin", "widgets"):
        shutil.copytree(os.path.join(QML_SRC, part), os.path.join(UI, part))
    for name in ("metrics.json", "pack.json"):
        shutil.copy2(os.path.join(QML_SRC, name), os.path.join(UI, name))
    for name in ("theme-light.json", "theme-dark.json"):
        shutil.copy2(os.path.join(STYLES, name), os.path.join(UI, name))

    # 叠加风格包（同名覆盖，其余保持内置）
    for base, _dirs, files in os.walk(pack):
        for f in files:
            src = os.path.join(base, f)
            rel = os.path.relpath(src, pack)
            dst = os.path.join(UI, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(src, dst)

    # metrics 在内置基线之上叠加（与宿主 ThemeBridge 的 overlay 语义一致）
    base_metrics = os.path.join(QML_SRC, "metrics.json")
    metrics = os.path.join(pack, "metrics.json")
    if not os.path.isfile(metrics):
        metrics = "none"
    palette = os.path.join(UI, "theme-%s.json" % mode)
    subprocess.run([PY, os.path.join(HARNESS, "gen_theme.py"), palette, base_metrics, metrics,
                    os.path.join(HARNESS, "ULinHub", "Theme.qml"), mode], check=True)
    subprocess.run([PY, os.path.join(HARNESS, "gen_qmldir.py"), UI], check=True,
                   stdout=subprocess.DEVNULL)

    localize_ui(load_translations())


def main():
    pack, mode, out = sys.argv[1:4]
    # ⚠️ 子进程的 cwd 是 HARNESS，相对路径会落在 HARNESS 里；统一转绝对路径再传
    out = os.path.abspath(out)
    pack = os.path.abspath(pack)
    rebuild_ui(pack, mode)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    env = dict(os.environ)
    env["QT_QUICK_BACKEND"] = "software"
    env["PATH"] = r"C:\Windows\System32;D:\Qt\6.11.2\msvc2022_64\bin;" + env.get("PATH", "")
    proc = subprocess.run([QML_EXE, "-I", HARNESS, os.path.join(HARNESS, "Preview.qml"),
                           "--out", out], cwd=HARNESS, env=env,
                          capture_output=True, text=True, timeout=120)
    err = (proc.stdout or "") + (proc.stderr or "")
    print(err.strip()[:4000])
    shrink(out)
    print("exit", proc.returncode, "->", out)


def shrink(path, max_width=1680):
    """预览图按窗口 DPR 抓出来是 2560 宽，带颗粒的包 PNG 会到 1.5MB；
    缩到 1680 宽既够看清细节，文件也小一半以上。"""
    try:
        from PIL import Image
    except ImportError:
        return
    if not os.path.isfile(path):
        return
    im = Image.open(path)
    if im.width <= max_width:
        return
    im = im.convert("RGB")
    h = round(im.height * max_width / im.width)
    im.resize((max_width, h), Image.LANCZOS).save(path, optimize=True)


if __name__ == "__main__":
    main()
