"""删除三份发布树里的 dsh-client-ui-* 与 libreoffice-kit-win32-x64。

用法（仓库根目录）：
  python misc/tools/prune-client-ui.sh.py            # 干跑
  python misc/tools/prune-client-ui.sh.py --apply    # 执行

删除前把每个 client-ui 包的 host 半区（package.json + lib/index.js）备份到
misc/prune-quarantine/client-ui-host-halves/<root>/<pkg>/，万一服务端启动时报
MODULE_NOT_FOUND，可据此精确恢复。
"""

import argparse
import json
import os
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(os.path.dirname(HERE))
QROOT = os.path.join(PROJECT, "misc", "prune-quarantine", "client-ui-host-halves")

ROOTS = [
    ("assets", os.path.join(PROJECT, "assets", "resources", "server")),
    ("release", os.path.join(PROJECT, "x64", "Release", "resources", "server")),
    ("releasebuild", os.path.join(PROJECT, "misc", "release-build", "bin", "resources", "server")),
]

# 整包删除的目标（相对 @deepseek-ai）
EXACT_TARGETS = ["libreoffice-kit-win32-x64"]
PREFIX_TARGETS = ["dsh-client-ui-"]
# 备份白名单：这些是 host 半区，恢复时够用
BACKUP_ITEMS = ["package.json", os.path.join("lib", "index.js")]


def dirsize(path):
    total = 0
    for base, _dirs, files in os.walk(path):
        for fn in files:
            try:
                total += os.path.getsize(os.path.join(base, fn))
            except OSError:
                pass
    return total


def collect(scope_dir):
    """返回 [(显示名, 绝对路径, 类别)]"""
    out = []
    if not os.path.isdir(scope_dir):
        return out
    for entry in sorted(os.listdir(scope_dir)):
        full = os.path.join(scope_dir, entry)
        if not os.path.isdir(full):
            continue
        if entry in EXACT_TARGETS:
            out.append(("@deepseek-ai/" + entry, full, "exact"))
        elif any(entry.startswith(p) for p in PREFIX_TARGETS):
            out.append(("@deepseek-ai/" + entry, full, "prefix"))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--limit", type=int, default=0, help="本次最多删多少个包（绕开批量删除阈值）")
    ap.add_argument("--tag", default="", help="只处理某个 root（assets/release/releasebuild）")
    args = ap.parse_args()

    plan = []
    grand = 0
    for tag, server in ROOTS:
        if args.tag and tag != args.tag:
            continue
        scope = os.path.join(server, "node_modules", "@deepseek-ai")
        items = collect(scope)
        if not items:
            print("[%s] 跳过：%s 不存在或无可删目标" % (tag, scope))
            continue
        total = sum(dirsize(p) for _n, p, _k in items)
        grand += total
        n_ui = sum(1 for _n, _p, k in items if k == "prefix")
        print("[%s] %d 个包（client-ui %d + libreoffice %d），%.1f MB"
              % (tag, len(items), n_ui, len(items) - n_ui, total / 1048576))
        plan.append((tag, items, total))

    if args.limit:
        budget = args.limit
        clipped = []
        for tag, items, total in plan:
            take = items[:budget]
            budget -= len(take)
            if take:
                clipped.append((tag, take, sum(dirsize(p) for _n, p, _k in take)))
            if budget <= 0:
                break
        plan = clipped
        print("（本次限额 %d 个）" % args.limit)

    print("\n合计可释放 %.1f MB" % (grand / 1048576))
    if not args.apply:
        print("干跑结束。确认后加 --apply 执行。")
        return

    stamp = time.strftime("%Y%m%d-%H%M%S")
    removed = 0
    freed = 0
    failed = []
    for tag, items, _t in plan:
        for name, path, kind in items:
            size = dirsize(path)
            if kind == "prefix":
                dst_root = os.path.join(QROOT, tag, os.path.basename(path))
                for rel in BACKUP_ITEMS:
                    src = os.path.join(path, rel)
                    if not os.path.isfile(src):
                        continue
                    dst = os.path.join(dst_root, rel)
                    os.makedirs(os.path.dirname(dst), exist_ok=True)
                    try:
                        shutil.copy2(src, dst)
                    except OSError:
                        pass
            try:
                shutil.rmtree(path)
                removed += 1
                freed += size
            except OSError as exc:
                failed.append((tag, name, str(exc)))
    print("\n已删除 %d 个包，释放 %.1f MB" % (removed, freed / 1048576))
    if failed:
        print("失败 %d 个:" % len(failed))
        for tag, name, exc in failed[:10]:
            print("   [%s] %s -> %s" % (tag, name, exc))
    else:
        print("全部成功。")

    log = os.path.join(PROJECT, "misc", "client-ui-removed-%s.json" % stamp)
    json.dump({tag: [n for n, _p, _k in items] for tag, items, _t in plan},
              open(log, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print("清单: %s" % log)
    print("host 半区备份: %s" % QROOT)


if __name__ == "__main__":
    main()
