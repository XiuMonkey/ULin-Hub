"""给 harness/ui 下每个目录补 qmldir（列出该目录的 qml 类型），让目录 import 在 qml.exe 里稳定解析。"""
import os
import sys

root = sys.argv[1]
for base, _dirs, files in os.walk(root):
    qmls = sorted(f[:-4] for f in files if f.endswith(".qml") and f[0].isupper())
    if not qmls:
        continue
    out = os.path.join(base, "qmldir")
    rel = os.path.relpath(base, root).replace("\\", "/")
    lines = []
    for name in qmls:
        lines.append("%s 1.0 %s.qml" % (name, name))
    open(out, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
    print("qmldir", rel)
