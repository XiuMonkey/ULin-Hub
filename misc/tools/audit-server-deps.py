# -*- coding: utf-8 -*-
"""分析 assets/resources/server/node_modules 里哪些包是 ULin Hub 客户端真正会加载的。

三层判定：
  A. 依赖闭包   = 从根包出发，沿 package.json 的 dependencies/peerDependencies/optionalDependencies 可达
  B. 导入闭包   = 从根包出发，沿 lib/**.js 里真实出现的 import / require / require.resolve 可达
  未被 B 覆盖 = node 侧一行代码都没引用过（多半只服务于官方 Web/browser 端的构建产物）

根包 = @deepseek-ai/dsh（CLI bin）+ 运行期 profile 里所有挂载行引用的包 + dsh-base bundle
"""
import json
import os
import re
import sys

ROOT = r"C:\Users\Playe\Documents\ULin\ULin Hub\assets\resources\server\node_modules"

# 运行期实际挂载的行（来自 dsh-base/cordis.patch.yml + 客户端生成的 profiles/web/cordis.patch.yml）
ROW_PACKAGES = [
    # dsh-base 行
    "@deepseek-ai/dsh-plugin-manager", "@deepseek-ai/cordis-plugin-timer", "@deepseek-ai/dsh-hmr",
    "@deepseek-ai/dsh-llm", "@deepseek-ai/dsh-deepseek-llm-api-extensions", "@deepseek-ai/dsh-session",
    "@deepseek-ai/dsh-session-log-deepseek", "@deepseek-ai/dsh-typert-registry", "@deepseek-ai/dsh-typert-loader",
    "@deepseek-ai/dsh-api-gateway", "@deepseek-ai/dsh-session-title", "@deepseek-ai/dsh-session-title-first-prompt-llm",
    "@deepseek-ai/dsh-user-questions", "@deepseek-ai/dsh-agent", "@deepseek-ai/dsh-plugin-package-inventory-deepseek",
    "@deepseek-ai/dsh-agent-default-model", "@deepseek-ai/dsh-jobs-local", "@deepseek-ai/dsh-llm-retry",
    "@deepseek-ai/dsh-config-editor", "@deepseek-ai/dsh-settings", "@deepseek-ai/dsh-authorization",
    "@deepseek-ai/dsh-deepseek-account-platform", "@deepseek-ai/dsh-credentials-local", "@deepseek-ai/dsh-llm-pi-ai",
    "@deepseek-ai/dsh-session-persistence-jsonl", "@deepseek-ai/dsh-attachment-local",
    "@deepseek-ai/dsh-session-query-sqlite", "@deepseek-ai/dsh-session-projection", "@deepseek-ai/dsh-storage",
    "@deepseek-ai/dsh-storage-json", "@deepseek-ai/dsh-storage-domain", "@deepseek-ai/dsh-session-projection-cache",
    "@deepseek-ai/dsh-otel", "@deepseek-ai/dsh-session-telemetry-otel", "@deepseek-ai/dsh-subprocess-local",
    "@deepseek-ai/dsh-sandbox-local", "@deepseek-ai/dsh-sandbox-policy", "@deepseek-ai/dsh-bash-sandbox",
    "@deepseek-ai/dsh-pwsh-sandbox", "@deepseek-ai/dsh-user-approval", "@deepseek-ai/dsh-permission-presets",
    "@deepseek-ai/dsh-shell-env", "@deepseek-ai/dsh-tool-bash", "@deepseek-ai/dsh-tool-pwsh",
    "@deepseek-ai/dsh-tool-jobs", "@deepseek-ai/dsh-fs-observation-policy", "@deepseek-ai/dsh-tool-fs",
    "@deepseek-ai/dsh-tool-fs-search", "@deepseek-ai/dsh-agent-instructions", "@deepseek-ai/dsh-skill",
    "@deepseek-ai/dsh-skill-filesystem", "@deepseek-ai/dsh-skill-badge", "@deepseek-ai/dsh-tool-skill",
    "@deepseek-ai/dsh-commands", "@deepseek-ai/dsh-command-feedback", "@deepseek-ai/dsh-goal",
    "@deepseek-ai/dsh-goal-round-driver", "@deepseek-ai/dsh-command-goal", "@deepseek-ai/dsh-plan-mode",
    "@deepseek-ai/dsh-token-meter", "@deepseek-ai/dsh-compaction-basic", "@deepseek-ai/dsh-command-compact",
    "@deepseek-ai/dsh-subagent", "@deepseek-ai/dsh-subagent-spawn-in-process",
    "@deepseek-ai/dsh-subagent-fork-in-process", "@deepseek-ai/dsh-tool-subagent-control",
    "@deepseek-ai/dsh-tool-subagent", "@deepseek-ai/dsh-ptc-runtime-node", "@deepseek-ai/dsh-workflow-ptc",
    "@deepseek-ai/dsh-tool-workflow", "@deepseek-ai/dsh-tool-call-timeout-policy", "@deepseek-ai/dsh-spill-local",
    "@deepseek-ai/dsh-spill-policy", "@deepseek-ai/dsh-session-checkpoint-policy",
    "@deepseek-ai/dsh-compaction-tool-result-pruner", "@deepseek-ai/dsh-compaction-image-offload",
    "@deepseek-ai/dsh-tool-todo", "@deepseek-ai/dsh-tool-goal", "@deepseek-ai/dsh-tool-ralph",
    "@deepseek-ai/dsh-repeat-tool-reminder", "@deepseek-ai/dsh-web", "@deepseek-ai/dsh-web-search-deepseek",
    "@deepseek-ai/dsh-web-fetch-http", "@deepseek-ai/dsh-tool-web", "@deepseek-ai/dsh-mcp-resources",
    "@deepseek-ai/dsh-tools", "@deepseek-ai/dsh-system-prompt", "@deepseek-ai/dsh-agent-loop",
    "@deepseek-ai/dsh-fs-sandbox", "@deepseek-ai/dsh-llm-deepseek-api-key", "@deepseek-ai/dsh-llm-deepseek-account",
    # 客户端 profile 的行
    "@deepseek-ai/dsh-workspace", "@deepseek-ai/dsh-host-plugin-inventory", "@deepseek-ai/dsh-api-session-controller",
    "@deepseek-ai/dsh-api-workspace-controller", "@deepseek-ai/dsh-api-settings-controller",
    "@deepseek-ai/dsh-api-workspace-files", "@deepseek-ai/dsh-cordis-host-runner",
    "@deepseek-ai/dsh-web-app", "@deepseek-ai/dsh-host-webserver", "@deepseek-ai/dsh-host-directory-picker-auto",
    "@deepseek-ai/dsh-client-connection", "@deepseek-ai/dsh-client-file-upload", "@deepseek-ai/dsh-api-remotes",
    "@deepseek-ai/dsh-session-stats", "@deepseek-ai/dsh-api-terminal-controller",
    "@deepseek-ai/dsh-api-job-controller", "dshmarket",
    # CLI 本体
    "@deepseek-ai/dsh", "@deepseek-ai/dsh-base",
]

IMPORT_RE = re.compile(
    r"""(?:from|import|require|require\.resolve)\s*\(?\s*["']([^"']+)["']"""
)


def pkg_dir_in(node_modules, name):
    return os.path.join(node_modules, *name.split("/"))


def read_json(path):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return None


def iter_js(pkg_dir):
    for base, dirs, files in os.walk(pkg_dir):
        dirs[:] = [d for d in dirs if d not in ("test", "tests", "__tests__", "src", "node_modules")]
        for fn in files:
            if fn.endswith((".js", ".mjs", ".cjs")):
                yield os.path.join(base, fn)


class Resolver:
    def __init__(self, root):
        self.root = root
        self.cache = {}

    def find(self, name, from_dir):
        """Node 风格：先看发起包自己的 node_modules，再向上到 server/node_modules。"""
        d = from_dir
        while True:
            cand = pkg_dir_in(os.path.join(d, "node_modules"), name)
            if os.path.isfile(os.path.join(cand, "package.json")):
                return cand
            parent = os.path.dirname(d)
            if parent == d or len(parent) < len(self.root) - 60:
                break
            d = parent
        cand = pkg_dir_in(self.root, name)
        if os.path.isfile(os.path.join(cand, "package.json")):
            return cand
        return None

    def name_of(self, path):
        rel = os.path.relpath(path, self.root).replace("\\", "/")
        parts = rel.split("/")
        if parts[0].startswith("@"):
            return "/".join(parts[:2])
        return parts[0]


def pkg_name_from_spec(spec):
    if spec.startswith("."):
        return None
    parts = spec.split("/")
    if spec.startswith("@"):
        return "/".join(parts[:2]) if len(parts) >= 2 else spec
    return parts[0]


def main():
    res = Resolver(ROOT)

    # ---- A 层：package.json 声明闭包 ----
    dep_seen = {}
    stack = []
    for name in ROW_PACKAGES:
        p = res.find(name, ROOT)
        if p is None:
            print(f"[miss-row] {name}", file=sys.stderr)
            continue
        stack.append(p)
    while stack:
        d = stack.pop()
        name = res.name_of(d)
        if name in dep_seen:
            continue
        dep_seen[name] = d
        man = read_json(os.path.join(d, "package.json")) or {}
        for key in ("dependencies", "peerDependencies", "optionalDependencies"):
            for dep in (man.get(key) or {}):
                sub = res.find(dep, d)
                if sub and res.name_of(sub) not in dep_seen:
                    stack.append(sub)

    # ---- B 层：真实 import 闭包 ----
    imp_seen = {}
    queue = [dep_seen[n] for n in ROW_PACKAGES if n in dep_seen]
    edges = {}
    while queue:
        d = queue.pop()
        name = res.name_of(d)
        if name in imp_seen:
            continue
        imp_seen[name] = d
        used = set()
        for js in iter_js(d):
            try:
                text = open(js, "r", encoding="utf-8", errors="ignore").read()
            except Exception:
                continue
            for spec in IMPORT_RE.findall(text):
                n = pkg_name_from_spec(spec)
                if not n:
                    continue
                sub = res.find(n, d)
                if sub:
                    used.add(n)
                    if n not in imp_seen and n not in [res.name_of(x) for x in queue]:
                        queue.append(sub)
        edges[name] = used

    # ---- 统计体积 ----
    def dirsize(p):
        total = 0
        for base, _dirs, files in os.walk(p):
            for fn in files:
                try:
                    total += os.path.getsize(os.path.join(base, fn))
                except OSError:
                    pass
        return total

    top = []
    for entry in sorted(os.listdir(ROOT)):
        p = os.path.join(ROOT, entry)
        if not os.path.isdir(p):
            continue
        if entry.startswith("@"):
            for sub in sorted(os.listdir(p)):
                sp = os.path.join(p, sub)
                if os.path.isdir(sp):
                    top.append((entry + "/" + sub, sp))
        else:
            top.append((entry, p))

    rows = []
    for name, p in top:
        rows.append({
            "name": name,
            "mb": dirsize(p) / 1048576.0,
            "in_dep": name in dep_seen,
            "in_import": name in imp_seen,
        })
    rows.sort(key=lambda r: -r["mb"])

    out = {"rows": rows, "edges": {k: sorted(v) for k, v in edges.items()},
           "dep_count": len(dep_seen), "import_count": len(imp_seen)}
    with open(r"C:\Users\Playe\Documents\ULin\ULin Hub\misc\server-dep-audit.json", "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=1)

    def summarise(pred, title):
        sel = [r for r in rows if pred(r)]
        print(f"\n== {title} == {len(sel)} 个包, {sum(r['mb'] for r in sel):.1f} MB")
        for r in sel:
            print(f"  {r['mb']:8.2f} MB  {r['name']}")

    summarise(lambda r: not r["in_dep"], "A. 连声明闭包都不在（完全孤立）")
    summarise(lambda r: r["in_dep"] and not r["in_import"], "B. 被声明但 node 侧从未 import")
    summarise(lambda r: r["in_import"], "C. 真实被 import（必须保留）")

    total = sum(r["mb"] for r in rows)
    print(f"\n合计 {len(rows)} 个包 {total:.1f} MB")


if __name__ == "__main__":
    main()
