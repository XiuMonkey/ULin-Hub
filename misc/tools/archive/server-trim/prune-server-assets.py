"""按审计结果裁剪 assets/resources/server 的发布体积。

用法（在仓库根目录执行）：

  python misc/tools/prune-server-assets.py                 # 干跑：只打印要删什么
  python misc/tools/prune-server-assets.py --apply         # 真正执行：整包移到 misc/prune-quarantine/<时间戳>/
  python misc/tools/prune-server-assets.py --apply --purge # 直接删除，不进隔离区（node_modules 可由 lock 重装）
  python misc/tools/prune-server-assets.py --restore misc/prune-quarantine/<时间戳>

裁剪依据见 misc/SERVER-TRIM-AUDIT.zh-CN.md，清单在 misc/server-prune-list.json。
重新 npm install 之后需要再跑一次（清单是按当前 lock 的版本算出来的）。
"""

import argparse
import json
import os
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(os.path.dirname(HERE))
SERVER = os.path.join(PROJECT, "assets", "resources", "server")
NM = os.path.join(SERVER, "node_modules")
LIST = os.path.join(PROJECT, "misc", "server-prune-list.json")
QUARANTINE = os.path.join(PROJECT, "misc", "prune-quarantine")

# 审计脚本会把它们判成"无人引用"，但运行时是按计算出来的名字加载的，删了就出功能故障。
PROTECT = {
    "@vscode/ripgrep-win32-x64",              # @vscode/ripgrep 拼平台名 require，全文搜索要用
    "node-addon-require-builtin-win32-x64-msvc",
    "@img/sharp-win32-x64",                   # sharp 拼平台名 require，附件图片处理要用
    "@koromix/koffi-win32-x64",               # koffi 的 FFI 后端
    "@deepseek-ai/dsh-web-frontend",          # dsh-web-app 启动时 require.resolve 它的 package.json
    "pnpm",                                   # 插件市场 /dsh-market/install 靠它装插件
    "fflate", "fontkit", "saxes", "koffi",    # libreoffice-kit 的运行时依赖
    "open", "cross-spawn", "default-browser", # web-app 打开浏览器那条链路
    "@deepseek-ai/dsh-client-modules",
    "@deepseek-ai/dsh-host-directory-picker",
    "@deepseek-ai/dsh-host-directory-picker-native",
    "@deepseek-ai/dsh-host-directory-picker-browse",
    "@deepseek-ai/dsh-client-ui-directory-picker-browse",
    "@deepseek-ai/dsh-client-ui-settings-plugin-inventory",
    "@deepseek-ai/dsh-headless",
    # 保守保留：1MB 级别，收益不值得冒风险
    "@opentelemetry/sdk-trace",
    "@earendil-works/pi-telemetry",
    "@babel/runtime",
}

# 保留包内部还能再刮掉的死文件（node 从不读它们）
JUNK_SUFFIX = (".d.ts", ".d.mts", ".d.cts", ".map", ".md", ".pdb")

# 许可证/版权文件不在清理范围内（即使叫 LICENSE.md 也保留）——分发合规要求
JUNK_KEEP_STEM = ("license", "licence", "copying", "notice", "authors",
                  "unlicense", "patents", "thirdpartynotices")


def dirsize(path):
    total = 0
    for base, _dirs, files in os.walk(path):
        for fn in files:
            try:
                total += os.path.getsize(os.path.join(base, fn))
            except OSError:
                pass
    return total


def load_delete_list():
    if not os.path.isfile(LIST):
        sys.exit("找不到清单 %s，先跑 misc/tools/audit-server-prune.py" % LIST)
    names = json.load(open(LIST, encoding="utf-8"))
    out = []
    for name in names:
        if name in PROTECT:
            continue
        path = os.path.join(NM, *name.split("/"))
        if os.path.isdir(path):
            out.append((name, path))
    return out


def is_license_like(fn):
    """LICENSE / LICENSE.md / LICENSE-MIT / COPYING.txt … 一律不碰。"""
    stem = fn.lower()
    for suf in JUNK_SUFFIX:
        if stem.endswith(suf):
            stem = stem[: -len(suf)]
            break
    stem = stem.strip(" .-_")
    for key in JUNK_KEEP_STEM:
        if stem == key or stem.startswith(key + "-") or stem.startswith(key + "."):
            return True
    return False


def load_junk_files(delete_paths, whole_tree=False):
    """保留包内部可清的 .d.ts/.map/.md/.pdb。

    whole_tree=False 时跳过要整包删掉的目录（反正整包会走）；
    whole_tree=True 时扫描整个 node_modules（只清死文件、不整包删的场景）。
    """
    if whole_tree:
        skip = set()
    else:
        skip = {os.path.normcase(p) for p in delete_paths}
    files = []
    for entry in sorted(os.listdir(NM)):
        tops = []
        if entry.startswith("@"):
            scope = os.path.join(NM, entry)
            tops = [os.path.join(scope, s) for s in sorted(os.listdir(scope))]
        else:
            tops = [os.path.join(NM, entry)]
        for top in tops:
            if not os.path.isdir(top) or os.path.normcase(top) in skip:
                continue
            for base, _dirs, names in os.walk(top):
                for fn in names:
                    if fn.lower().endswith(JUNK_SUFFIX) and not is_license_like(fn):
                        p = os.path.join(base, fn)
                        try:
                            files.append((p, os.path.getsize(p)))
                        except OSError:
                            pass
    return files


def restore(quarantine_dir):
    if not os.path.isdir(quarantine_dir):
        sys.exit("隔离目录不存在: %s" % quarantine_dir)
    moved = 0
    for base, _dirs, files in os.walk(quarantine_dir):
        rel = os.path.relpath(base, quarantine_dir)
        for fn in files:
            src = os.path.join(base, fn)
            dst = os.path.join(NM, rel, fn) if rel != "." else os.path.join(NM, fn)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.move(src, dst)
            moved += 1
    print("已还原 %d 个文件到 %s" % (moved, NM))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apply", action="store_true", help="真正执行（默认只干跑）")
    ap.add_argument("--purge", action="store_true", help="直接删除而不进隔离区")
    ap.add_argument("--no-junk", action="store_true", help="只删整包，不清理包内 .d.ts/.map/.md")
    ap.add_argument("--junk-only", action="store_true",
                    help="只清理包内死文件(.d.ts/.map/.md/.pdb)，不删任何整包")
    ap.add_argument("--restore", metavar="DIR", help="从隔离目录还原")
    args = ap.parse_args()

    if args.restore:
        restore(args.restore)
        return

    if not os.path.isdir(NM):
        sys.exit("找不到 %s" % NM)

    before_nm = dirsize(NM)
    before_server = dirsize(SERVER)

    deletes = [] if args.junk_only else load_delete_list()
    del_size = sum(dirsize(p) for _n, p in deletes)
    junk = [] if args.no_junk else load_junk_files(
        [p for _n, p in deletes], whole_tree=args.junk_only)
    junk_size = sum(s for _p, s in junk)

    print("server 目录: %.1f MB（其中 node_modules %.1f MB）" % (before_server / 1048576, before_nm / 1048576))
    print("整包删除: %d 个, %.1f MB" % (len(deletes), del_size / 1048576))
    print("包内清理: %d 个文件, %.1f MB" % (len(junk), junk_size / 1048576))
    print("预计剩余: %.1f MB" % ((before_server - del_size - junk_size) / 1048576))
    print()
    if args.junk_only:
        buckets = {}
        for p, s in junk:
            low = p.lower()
            if low.endswith((".d.ts", ".d.mts", ".d.cts")):
                k = ".d.ts 类型声明"
            elif low.endswith(".map"):
                k = ".map sourcemap"
            elif low.endswith(".md"):
                k = ".md 文档"
            elif low.endswith(".pdb"):
                k = ".pdb 调试符号"
            else:
                k = "其它"
            n, sz = buckets.get(k, (0, 0))
            buckets[k] = (n + 1, sz + s)
        for k, (n, sz) in sorted(buckets.items(), key=lambda x: -x[1][1]):
            print("  %-16s %7d 个  %8.1f MB" % (k, n, sz / 1048576))
        print()
    for name, path in deletes[:20]:
        print("  %8.2f MB  %s" % (dirsize(path) / 1048576, name))
    if len(deletes) > 20:
        print("  ... 另有 %d 个（完整清单见 misc/server-prune-list.json）" % (len(deletes) - 20))

    if not args.apply:
        print("\n干跑结束。确认无误后加 --apply 执行。")
        return

    stamp = time.strftime("%Y%m%d-%H%M%S")
    target = os.path.join(QUARANTINE, stamp)
    mode = "删除" if args.purge else "隔离"
    print("\n开始%s ..." % mode)

    if not args.purge:
        os.makedirs(target, exist_ok=True)

    for name, path in deletes:
        dest = os.path.join(target, *name.split("/"))
        if args.purge:
            shutil.rmtree(path)
        else:
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            shutil.move(path, dest)

    removed = 0
    removed_size = 0
    failed = []
    for p, s in junk:
        if args.purge:
            try:
                os.remove(p)
                removed += 1
                removed_size += s
            except OSError as exc:
                failed.append((p, str(exc)))
        else:
            rel = os.path.relpath(p, NM)
            dest = os.path.join(target, "_junk", rel)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            shutil.move(p, dest)
            removed += 1
            removed_size += s

    if args.purge and junk:
        log = os.path.join(PROJECT, "misc", "server-junk-removed-%s.tsv" % stamp)
        with open(log, "w", encoding="utf-8") as fh:
            fh.write("# 已删除的死文件清单（相对 node_modules 的路径\tsize_bytes）\n")
            for p, s in junk:
                fh.write("%s\t%d\n" % (os.path.relpath(p, NM), s))
        print("删除清单: %s" % log)
        if failed:
            print("失败 %d 个（通常是文件被占用，关掉客户端后重跑即可）:" % len(failed))
            for p, exc in failed[:10]:
                print("   %s  (%s)" % (os.path.relpath(p, NM), exc))

    after_server = dirsize(SERVER)
    print("完成：清掉 %d 个包 / %d 个文件(%.1f MB)，server %.1f MB -> %.1f MB"
          % (len(deletes), removed, removed_size / 1048576,
             before_server / 1048576, after_server / 1048576))
    if not args.purge:
        print("隔离区: %s（--restore 可还原）" % target)
    print("\n下一步：启动一次客户端，确认日志里有 'dsh web: http://127.0.0.1:' 与 'auth cookie acquired'。")


if __name__ == "__main__":
    main()
