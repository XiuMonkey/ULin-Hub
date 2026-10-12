"""QML 静态守卫：找出 (1) 引用了不存在的 Theme token (2) 自我锚定 (3) 可疑的 parent/自身引用。

用法: python misc/tools/qml-token-guard.py [--fix-report]
退出码: 0 = 干净, 1 = 发现问题
"""
import io
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
QML_ROOT = os.path.join(REPO, "source", "qml")
PALETTE_DIR = os.path.join(REPO, "assets", "resources", "styles")
METRICS = os.path.join(QML_ROOT, "metrics.json")

palette_keys = set()
for name in ("theme-light.json", "theme-dark.json"):
    path = os.path.join(PALETTE_DIR, name)
    if os.path.exists(path):
        palette_keys |= set(json.load(io.open(path, encoding="utf-8")).keys())

metrics = json.load(io.open(METRICS, encoding="utf-8"))
metric_groups = {k: set(v.keys()) for k, v in metrics.items() if isinstance(v, dict)}

problems = []


def strip_comment(line):
    """去掉行尾 // 注释，但不动字符串里的 //（例如 "http://…"）。

    必须做这一步：否则"在注释里引用旧代码"会被当成真代码报出来
    （PluginMarketPage.qml 里就有一处说明性的注释被误报过）。
    """
    in_str = False
    i = 0
    while i < len(line) - 1:
        c = line[i]
        if c == '"' and (i == 0 or line[i - 1] != "\\"):
            in_str = not in_str
        elif not in_str and c == "/" and line[i + 1] == "/":
            return line[:i]
        i += 1
    return line


def qml_files():
    for dp, _, fs in os.walk(QML_ROOT):
        for f in fs:
            if f.endswith(".qml"):
                yield os.path.join(dp, f)


# ---- 1) Theme token 引用 ----
token_re = re.compile(r"Theme\.([A-Za-z]+)\.([A-Za-z_][A-Za-z0-9_]*)")
for path in qml_files():
    rel = os.path.relpath(path, REPO).replace(os.sep, "/")
    for n, raw in enumerate(io.open(path, encoding="utf-8"), 1):
        line = strip_comment(raw)
        for m in token_re.finditer(line):
            group, key = m.group(1), m.group(2)
            if group == "color":
                if key not in palette_keys:
                    problems.append((rel, n, "color", key, raw.strip()))
            elif group in metric_groups:
                if key not in metric_groups[group]:
                    problems.append((rel, n, group, key, raw.strip()))

# ---- 2) 自我锚定：某对象的 anchors 引用了它自己的 id ----
# 注意：子项写 `anchors.margins: root.padding`（引用父项/根）是合法的，
# 只有"锚点与 id: 是同一个对象的兄弟属性"才是 Qt 报的 Cannot anchor item to self。
id_re = re.compile(r"^\s*id:\s*([A-Za-z_]\w*)\s*$")
anchor_re = re.compile(r"anchors\.[a-zA-Z]+\s*:\s*([A-Za-z_]\w*)\s*\.")
for path in qml_files():
    rel = os.path.relpath(path, REPO).replace(os.sep, "/")
    lines = io.open(path, encoding="utf-8").read().split("\n")
    depth = 0
    id_at_depth = {}          # 当前深度的对象所声明的 id
    for n, raw in enumerate(lines, 1):
        line = strip_comment(raw)
        mid = id_re.match(line)
        if mid:
            id_at_depth[depth] = mid.group(1)
        am = anchor_re.search(line)
        if am and id_at_depth.get(depth) == am.group(1):
            problems.append((rel, n, "self-anchor", f"{am.group(1)} (自己的 id)", raw.strip()))
        depth += line.count("{") - line.count("}")
        for d in [d for d in id_at_depth if d > depth]:
            del id_at_depth[d]

# ---- 3) 报告 ----
if not problems:
    print("OK 没有发现不存在的 token、自我锚定")
    sys.exit(0)

print(f"发现 {len(problems)} 处问题：\n")
by_kind = {}
for rel, n, kind, key, src in problems:
    by_kind.setdefault(kind, []).append((rel, n, key, src))
for kind in sorted(by_kind):
    print(f"--- {kind} ({len(by_kind[kind])} 处) ---")
    for rel, n, key, src in by_kind[kind]:
        print(f"  {rel}:{n}  [{key}]  {src[:96]}")
    print()
sys.exit(1)
