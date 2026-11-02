"""清掉三份发布树 node_modules 里的死文件：.d.ts / .map / .md / .pdb。

node 运行时从不读这些（sourcemap 只有浏览器 DevTools 用，且 dsh-client-modules
的 readSourceMap 对 ENOENT 有容错）。许可证类文件（LICENSE/COPYING/NOTICE…）
一律保留，即使扩展名是 .md。

用法：
  python misc/tools/junk-sweep.py            # 干跑
  python misc/tools/junk-sweep.py --apply    # 执行
"""

import argparse
import os
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(os.path.dirname(HERE))

ROOTS = [
    ("assets", os.path.join(PROJECT, "assets", "resources", "server", "node_modules")),
    ("release", os.path.join(PROJECT, "x64", "Release", "resources", "server", "node_modules")),
    ("releasebuild", os.path.join(PROJECT, "misc", "release-build", "bin", "resources", "server", "node_modules")),
]

JUNK_SUFFIX = (".d.ts", ".d.mts", ".d.cts", ".map", ".md", ".pdb")
KEEP_STEM = ("license", "licence", "copying", "notice", "authors",
             "unlicense", "patents", "thirdpartynotices")


def is_license_like(fn):
    stem = fn.lower()
    for suf in JUNK_SUFFIX:
        if stem.endswith(suf):
            stem = stem[: -len(suf)]
            break
    stem = stem.strip(" .-_")
    for key in KEEP_STEM:
        if stem == key or stem.startswith(key + "-") or stem.startswith(key + "."):
            return True
    return False


def scan(nm):
    out = []
    for base, _dirs, files in os.walk(nm):
        for fn in files:
            if fn.lower().endswith(JUNK_SUFFIX) and not is_license_like(fn):
                p = os.path.join(base, fn)
                try:
                    out.append((p, os.path.getsize(p)))
                except OSError:
                    pass
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apply", action="store_true")
    args = ap.parse_args()

    grand_n = grand_b = 0
    total = []
    for tag, nm in ROOTS:
        if not os.path.isdir(nm):
            print("[%s] 跳过：%s 不存在" % (tag, nm))
            continue
        files = scan(nm)
        size = sum(s for _p, s in files)
        grand_n += len(files)
        grand_b += size
        total.append((tag, files, size))
        print("[%s] %d 个文件, %.1f MB" % (tag, len(files), size / 1048576))

    print("\n合计 %d 个文件, %.1f MB" % (grand_n, grand_b / 1048576))
    if not args.apply:
        print("干跑结束。确认后加 --apply 执行。")
        return

    stamp = time.strftime("%Y%m%d-%H%M%S")
    done = 0
    freed = 0
    failed = 0
    for tag, files, _s in total:
        for p, s in files:
            try:
                os.remove(p)
                done += 1
                freed += s
            except OSError:
                failed += 1

    print("已删除 %d 个文件, 释放 %.1f MB" % (done, freed / 1048576))
    if failed:
        print("失败 %d 个（多为被占用）" % failed)

    log = os.path.join(PROJECT, "misc", "junk-sweep-%s.tsv" % stamp)
    with open(log, "w", encoding="utf-8") as fh:
        for tag, files, _s in total:
            nm = dict(ROOTS)[tag]
            for p, s in files:
                fh.write("%s\t%s\t%d\n" % (tag, os.path.relpath(p, nm), s))
    print("清单: %s" % log)


if __name__ == "__main__":
    main()
