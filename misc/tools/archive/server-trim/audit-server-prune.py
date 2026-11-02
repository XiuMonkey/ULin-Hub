# -*- coding: utf-8 -*-
"""在 ULin Hub 客户端真实运行路径上，判定 assets/resources/server/node_modules 里
每个包属于：核心（node 侧真实 import）/ 被核心包按名字提到（可能动态加载，须留）/ 可删。

输出：misc/server-prune-report.json + 控制台摘要
"""
import json
import os
import re
from collections import defaultdict

ROOT = r"C:\Users\Playe\Documents\ULin\ULin Hub\assets\resources\server\node_modules"
MANIFEST = r"C:\Users\Playe\Documents\ULin\ULin Hub\misc\server-dep-audit.json"

# 客户端 profile 实际挂载的包（dsh-base bundle 行 + profiles/web/cordis.patch.yml 行）
MOUNTED = json.load(open(r"C:\Users\Playe\Documents\ULin\ULin Hub\misc\mounted-packages.json", encoding="utf-8")) \
    if os.path.exists(r"C:\Users\Playe\Documents\ULin\ULin Hub\misc\mounted-packages.json") else []

IMPORT_RE = re.compile(
    r"""(?:\bfrom\s*|\bimport\s*\(\s*|\brequire\s*\(\s*|\b[A-Za-z]*[Rr]equire\s*\(\s*|\bimport\b\s*)["']([^"'\n]+)["']"""
)


def pkg_name(spec):
    if spec.startswith(".") or spec.startswith("/") or spec.startswith("node:"):
        return None
    parts = spec.split("/")
    if spec.startswith("@"):
        return "/".join(parts[:2]) if len(parts) >= 2 else spec
    return parts[0]


def find(name, from_dir):
    d = from_dir
    while True:
        cand = os.path.join(d, "node_modules", *name.split("/"))
        if os.path.isfile(os.path.join(cand, "package.json")):
            return cand
        parent = os.path.dirname(d)
        if parent == d or not parent.startswith(os.path.dirname(ROOT)):
            break
        d = parent
    cand = os.path.join(ROOT, *name.split("/"))
    if os.path.isfile(os.path.join(cand, "package.json")):
        return cand
    return None


def walk_js(pkg_dir):
    for base, dirs, files in os.walk(pkg_dir):
        dirs[:] = [d for d in dirs if d not in ("node_modules", "test", "tests", "__tests__")]
        for fn in files:
            if fn.endswith((".js", ".mjs", ".cjs")):
                yield os.path.join(base, fn)


def dirsize(p):
    t = 0
    for base, _d, files in os.walk(p):
        for fn in files:
            try:
                t += os.path.getsize(os.path.join(base, fn))
            except OSError:
                pass
    return t


def main():
    # 根：CLI + 所有挂载行的包
    roots = ["@deepseek-ai/dsh", "@deepseek-ai/dsh-base"] + MOUNTED
    core = {}
    queue = []
    for n in roots:
        p = find(n, ROOT)
        if p:
            queue.append(p)
        else:
            print(f"[warn] 根包缺失 {n}")

    text_cache = {}
    while queue:
        d = queue.pop()
        name = os.path.relpath(d, ROOT).replace("\\", "/")
        if name.startswith("@"):
            name = "/".join(name.split("/")[:2])
        else:
            name = name.split("/")[0]
        if name in core:
            continue
        core[name] = d
        for js in walk_js(d):
            try:
                text = open(js, "r", encoding="utf-8", errors="ignore").read()
            except Exception:
                continue
            text_cache[js] = text
            for spec in IMPORT_RE.findall(text):
                n = pkg_name(spec)
                if not n:
                    continue
                sub = find(n, d)
                if sub:
                    key = os.path.relpath(sub, ROOT).replace("\\", "/").split("/node_modules/")[-1]
                    if key not in core:
                        queue.append(sub)

    # 核心包 js 全文并起来一次（用于“提到”判定）
    blob = "\n".join(text_cache.values())

    # 所有顶层包
    tops = []
    for entry in sorted(os.listdir(ROOT)):
        p = os.path.join(ROOT, entry)
        if not os.path.isdir(p):
            continue
        if entry.startswith("@"):
            for sub in sorted(os.listdir(p)):
                sp = os.path.join(p, sub)
                if os.path.isdir(sp):
                    tops.append((entry + "/" + sub, sp))
        else:
            tops.append((entry, p))

    out = []
    for name, p in tops:
        if name in core:
            kind = "core"
            referrers = []
        else:
            # 前缀匹配：既覆盖 "pkg" 也覆盖 "pkg/subpath" / "pkg/package.json"
            quoted = f'"{name}"' in blob or f"'{name}'" in blob \
                or f'"{name}/' in blob or f"'{name}/" in blob
            if quoted:
                kind = "mentioned"
                referrers = [k for k, d in core.items()
                             if any(f'"{name}' in t or f"'{name}" in t for t in text_cache.values())]
            else:
                kind = "orphan"
                referrers = []
        out.append({"name": name, "mb": round(dirsize(p) / 1048576.0, 2), "kind": kind,
                    "referrers": referrers[:4]})

    out.sort(key=lambda r: -r["mb"])
    json.dump({"packages": out, "core_count": len(core)},
              open(r"C:\Users\Playe\Documents\ULin\ULin Hub\misc\server-prune-report.json", "w", encoding="utf-8"),
              ensure_ascii=False, indent=1)

    for kind in ("orphan", "mentioned", "core"):
        sel = [r for r in out if r["kind"] == kind]
        print(f"\n== {kind} == {len(sel)} 个, {sum(r['mb'] for r in sel):.1f} MB")
        for r in sel:
            if kind != "core" or r["mb"] > 1:
                tip = (" <- " + ",".join(r["referrers"])) if r["referrers"] else ""
                print(f"  {r['mb']:8.2f}  {r['name']}{tip}")


if __name__ == "__main__":
    main()
