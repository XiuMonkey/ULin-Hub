# 服务端资源裁剪记录

日期：2026-10-24
对象：`assets/resources/server`、`x64/Release/resources/server`、`misc/release-build/bin/resources/server`
目的：去掉只有官方 DSH Web 版才用、ULin Hub Qt 客户端从不加载的内容。

## 一、结果

| 位置 | 裁剪前 | 裁剪后 | 减少 |
|---|---|---|---|
| `assets/resources/server` | 562.6 MB | 250.8 MB | -311.8 MB（-55%） |
| `x64/Release/resources/server` | ~581 MB | 265.6 MB | 约 -315 MB |
| `misc/release-build/bin/resources/server` | ~570 MB | 254.5 MB | 约 -315 MB |

三处合计释放约 **940 MB**。

`assets/resources/server` 明细：`node_modules` 476.7 → 164.8 MB，`node.exe` 85.5 MB 不变。

## 二、删除内容

### A. 整包删除 54 个（每处 28 + 182 = 210 MB）

**`@deepseek-ai/libreoffice-kit-win32-x64`（182 MB）** —— LibreOffice 转换引擎，服务端把 docx 转 PDF 供**官方 Web 前端**预览。客户端右栏预览由 Qt 自绘，源码里零处 pdf/office 调用；该包本身是 `optionalDependencies`，只在真正执行转换时惰性 resolve。

**`@deepseek-ai/dsh-client-ui-*` 共 53 个（28 MB）** —— React 写的浏览器界面插件：

```
dsh-client-ui-agent-preset          dsh-client-ui-settings
dsh-client-ui-approval              dsh-client-ui-settings-account
dsh-client-ui-attachment            dsh-client-ui-settings-agent-loop
dsh-client-ui-brand-official        dsh-client-ui-settings-general
dsh-client-ui-chat                  dsh-client-ui-settings-models
dsh-client-ui-commands              dsh-client-ui-settings-plugin-inventory
dsh-client-ui-conversation          dsh-client-ui-settings-plugins
dsh-client-ui-cordis                dsh-client-ui-settings-session-log
dsh-client-ui-deliverables          dsh-client-ui-settings-shell
dsh-client-ui-directory-picker-browse   dsh-client-ui-settings-subagent
dsh-client-ui-directory-picker-native   dsh-client-ui-settings-web-search
dsh-client-ui-goal                  dsh-client-ui-shortcuts
dsh-client-ui-input-trigger         dsh-client-ui-sidebar
dsh-client-ui-jobs                  dsh-client-ui-sidebar-browser
dsh-client-ui-layout                dsh-client-ui-sidebar-documentpreview
dsh-client-ui-message-feedback      dsh-client-ui-sidebar-files
dsh-client-ui-model-selection       dsh-client-ui-sidebar-right
dsh-client-ui-open-in-app           dsh-client-ui-sidebar-terminal
dsh-client-ui-permission-presets    dsh-client-ui-skill
dsh-client-ui-plan                  dsh-client-ui-slots
dsh-client-ui-plugin-manager        dsh-client-ui-subagent
dsh-client-ui-primitives            dsh-client-ui-theme
dsh-client-ui-reference             dsh-client-ui-tool
dsh-client-ui-renderer              dsh-client-ui-trajectory
dsh-client-ui-schedule              dsh-client-ui-user-questions
dsh-client-ui-session               dsh-client-ui-workflow-run
dsh-client-ui-workspace
```

（完整名单以 `assets/resources/server/package-lock.json` 里 `node_modules/@deepseek-ai/dsh-client-ui-` 前缀为准，可随时还原核对。）

### B. 死文件（每处约 100 MB）

| 类型 | assets | release | release-build |
|---|---|---|---|
| `.d.ts` / `.d.mts` / `.d.cts` 类型声明 | 8673 个 | — | — |
| `.map` sourcemap | 4344 个 | — | — |
| `.pdb` 调试符号（node-pty conpty） | 4 个 | — | — |
| **合计（删包后统计）** | **12006 个 / 98.8 MB** | **12981 个 / 105.5 MB** | **12981 个 / 105.5 MB** |

node 运行时从不读这三类文件。唯一的读取点是 `@deepseek-ai/dsh-client-modules` 的 `readSourceMap()`，它显式捕获 `ENOENT` 返回 `undefined`——缺了只是浏览器 DevTools 看不到源映射，而客户端根本不走浏览器。

**`.md` 未删**：每处仅 8～9 MB，打包压缩后更小，且 `LICENSE.md` / `COPYING.md` 属于分发合规文件。

## 三、为什么可以删（复核依据）

关键不是"包名出现过没有"，而是 **profile 到底挂载了什么**：

1. 客户端用 `node.exe → harness-node-entry.mjs → dsh/lib/bin.js web --port 0` 启动，profile 目录 `harness/profiles/web`。
2. 该 profile 的 `package.json` 里 `dsh.profile.bundles` 只有：`@deepseek-ai/dsh-base`、`dshmarket`、`dsh-outsourcing-expert`、`@goodandready/dsh-context-lens`、`@goodandready/dsh-time-machine`、`dsh-plugin-uisfx`。
3. **`@deepseek-ai/dsh-web-app` 不在 bundles 里**。它自带的 `cordis.patch.yml` 里那 48 行 `dsh-client-ui-*` 因此**不会被展开**——我们只是在 profile patch 里把 `dsh-web-app` 当普通插件挂了一行（`id: web-runtime`）。
4. 上述 6 个 bundle 的 `cordis.patch.yml` 中都没有 `dsh-client-ui-*` 引用（仅 3 个 experimental 包各引 1 行，而它们同样不在 bundles 里）。
5. 静态复查：删除后 `grep "dsh-client-ui-"`（排除各包自身的 `client.js`）在全部 host 侧 `lib/*.js` 里零命中；`libreoffice-kit-win32-x64` 同样零命中。

**反向陷阱（重要，下次别踩）**：`dsh-client-ui-*` 包的 `lib/index.js` 是 host 半区，有真实代码（如 `dsh-client-ui-chat` 会 `apply(ctx)` 注册 settings schema）；`lib/client.js` 才是浏览器 UI。所以**不能仅凭"我们用 Qt 不用浏览器"就断定这些包是纯前端死重量**——如果哪天有人把 `@deepseek-ai/dsh-web-app` 加进 profile 的 `bundles`，整包删除会立刻让 loader import 失败、服务端起不来。判断必须落在 `bundles` 上。

## 四、明确保留（评估过，别误删）

| 包 | 体积 | 理由 |
|---|---|---|
| `node.exe` | 85.5 MB | 运行时本体 |
| `pnpm` | 23.0 MB | 插件市场 `/dsh-market/install` 靠它装插件 |
| `@img/sharp-win32-x64` | 18.9 MB | 剪贴板贴图/附件图片处理，靠拼平台名 require |
| `@mixmark-io/domino` | 7.4 MB | `turndown` 的依赖，`dsh-tool-web` 在用 |
| `node-pty` | 6.9 MB | 终端 PTY，`createLazyRequire` 加载（静态扫描会误判成孤儿） |
| `dsh-web-frontend` | 5.4 MB | `dsh-web-app` 启动时 `require.resolve` 它的 `package.json`，缺了直接抛异常 |
| `@vscode/ripgrep-win32-x64` | 5.2 MB | 全文搜索，拼平台名 require |
| `openai` | 3.5 MB | `dsh-llm-pi-ai` 静态 import；该插件在 dsh-base patch 里是**挂载**的（休眠，无 settings 时零路由） |
| `@opentelemetry/*` | ~12 MB | 被挂载链路引用，收益不值得冒险 |

## 五、评估过但本次未删（留档）

这些确认引用很弱，但按"先不动"处理：

| 项 | 体积 | 说明 |
|---|---|---|
| `sherpa-onnx-win-x64` | 22.4 MB | 实验性语音转写，零引用，客户端无麦克风入口 |
| `@google/genai` / `@anthropic-ai/sdk` / `@aws-sdk` | 12.4 MB | 零引用；`openai` 不能动（pi-ai 静态 import），这三个可以 |
| `@agentclientprotocol/sdk` + `@deepseek-ai/dsh-acp` | 2.9 MB | 只被 `dsh-acp` 引用，而 profile 没挂 acp 行 |
| `@modelcontextprotocol/*` + `dsh-mcp-client` | 4.7 MB | 同上，profile 挂的是 `mcp-resources`，不是 `mcp-client` |
| `libreoffice-kit` 主包 + `fontkit` + `saxes` | ~6 MB | 平台包已删，这条转换链本来就跑不起来 |
| `@opentelemetry/semantic-conventions` | 3.6 MB | 纯枚举数据 |
| `.md` 文档 | 每处 8～9 MB | 合规与收益权衡后保留 |

## 六、怎么回滚

- 整包与死文件：`npm ci`（`assets/resources/server` 下有完整 `package-lock.json`）会按 lock 重装回 476.7 MB 的原状。
- `x64/Release` 与 `misc/release-build` 是构建/打包产物，直接用 `assets` 重新构建即可，不必单独修。

## 七、相关脚本

| 脚本 | 用途 |
|---|---|
| `misc/tools/audit-server-deps.py` | import 闭包审计：从 profile 挂载行反推真实被加载的包 |
| `misc/tools/audit-server-prune.py` | 分诊：区分"没人引用"与"只是没被 import" |
| `misc/tools/prune-client-ui.py` | 删除 `dsh-client-ui-*` 与 `libreoffice-kit-win32-x64`（三处，支持 `--limit` 分批） |
| `misc/tools/prune-junk.bat` | 用 cmd `del /s` 批量清 `.d.ts/.map/.mts/.cts/.pdb`（三处） |
| `misc/tools/junk-sweep.py` | 同上功能的 Python 版（逐文件删，Windows 上很慢，约 9 文件/秒） |
| `misc/tools/prune-server-assets.py` | 早期方案：整包删除 + 死文件清理，默认干跑 |
