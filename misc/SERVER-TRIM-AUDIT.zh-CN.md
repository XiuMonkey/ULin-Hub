# 内置服务端资源瘦身审计（assets/resources/server）

> 结论先说：**562.6 MB → 218.8 MB（-61%）**，删掉 131 个包 + 1.2 万个包装里的死文件，
> 其中 182 MB 是 LibreOffice 转换引擎，整块与 ULin Hub 客户端无关。
> 判定不靠猜：从客户端 `profiles/web/cordis.patch.yml` 实际挂载的行出发做 import 闭包，
> 再对每个包查"是否被任何已加载代码引用过"。

审计脚本：`misc/tools/audit-server-deps.py`（声明闭包）、`misc/tools/audit-server-prune.py`（引用闭包 + 分诊）
裁剪脚本：`misc/tools/prune-server-assets.py`（默认干跑）
可删清单：`misc/server-prune-list.json`

---

## 一、现状账本

| 项 | 体积 |
|---|---|
| `node_modules/` | 476.7 MB |
| `node.exe` | 89.7 MB |
| `package-lock.json` + `launch-root/` | 0.4 MB |
| **合计** | **562.6 MB**（资源管理器 622 MB，差值来自簇大小） |

`node_modules` 按文件类型拆开看，第一刀就能砍掉的死文件：

| 类型 | 文件数 | 体积 | 说明 |
|---|---|---|---|
| `exe/dll` | 21 | 307.3 MB | 真正需要留的只有 node-pty / sharp / ripgrep / koffi |
| `js` | 9 573 | 107.3 MB | 真正会被执行的代码 |
| `*.d.ts` | 8 674 | 40.4 MB | 纯 TypeScript 类型声明，node 从不读 |
| `*.map` | 4 344 | 39.7 MB | sourcemap，不打 `--enable-source-maps` 就用不到 |
| `*.pdb` | 4 | 19.8 MB | 原生模块的调试符号 |
| `*.md` | 967 | 8.5 MB | 各包 README |

---

## 二、判定依据（可复核）

客户端启动链路是：`node.exe resources/node/harness-node-entry.mjs
resources/server/node_modules/@deepseek-ai/dsh/lib/bin.js web --port 0`
（见 `source/src/core/ServerManager.cpp::launchBundledServer`），profile 只挂载两部分：

1. `@deepseek-ai/dsh-base` bundle 的 ~95 行（`dsh-base/cordis.patch.yml`）
2. 客户端自己插入的 ~20 行（workspace / api-* 控制器 / webserver / web-app / connection / terminal / job / session-stats / dshmarket / ToolsFilterPlugin）

从这 110 个挂载包出发做 `import` 闭包，**362 个包是真实被加载的**（core）；
剩下 138 个包，其包名在任何已加载的 `lib/*.js` 里**一次都没出现过** —— 这些就是"官方 DSH 有、我们的客户端用不到"的部分。
再扣掉 2 个靠拼接名字加载的平台二进制包（ripgrep / node-addon）和 5 个 1 MB 级、收益不值得冒风险的保守保留项
（`@opentelemetry/sdk-trace`、`@babel/runtime`、`cross-spawn`、`default-browser`、`@earendil-works/pi-telemetry`），
最终 **131 个包进删除清单**。

---

## 三、A 档：整包删除（259.6 MB / 131 个）

| 主题 | 包数 | 体积 | 为什么用不到 |
|---|---|---|---|
| **LibreOffice 转换引擎** `libreoffice-kit-win32-x64` | 1 | **182.0** | 服务端把 docx/xlsx/pptx 转 PDF 给**官方 Web 前端的文档预览**用。我们的右栏预览是 Qt 自己画的，全仓库源码里没有一处 pdf/office 调用；而且它是 `optionalDependencies`，只在真的转换时才惰性 resolve，删掉不影响启动 |
| **官方 Web 前端 / 浏览器 UI** `dsh-client-ui-*`（54 个） | 54 | 27.6 | React 写的浏览器界面插件。我们的 `?token=` 握手在 `dsh-client-connection` 的 `authorizeIndex` 里就 303 回掉了，根本不读页面；这些包只服务于 `http://127.0.0.1:port` 那个官方 Web UI。最大项 `sidebar-documentpreview` 13.9 MB |
| **语音输入 / STT** `sherpa-onnx-win-x64` 等 | 6 | 22.8 | 实验性语音转写，客户端没有麦克风入口 |
| **GitHub / Webhook / Hooks** `@octokit/*`、`dsh-webhook*`、`dsh-hooks-*` | 12 | 8.7 | GitHub webhook 与 Claude/Codex 的 hook 兼容层，都没挂行 |
| **MCP 客户端** `@modelcontextprotocol/*`、`dsh-mcp-client` | 5 | 7.8 | MCP 是客户端连外部工具用的协议；我们没挂 `mcp-client` 行（挂的是 `mcp-resources`，那个留着）。将来要做 MCP 再装回 |
| **ACP 编辑器协议** `@agentclientprotocol/sdk`、`dsh-acp*` | 2 | 5.4 | Zed 那套 agent-client-protocol，和 Qt 客户端无关 |
| **纯类型包** `@types/node`、`type-fest`、`ts-algebra` 等 | 8 | 3.2 | 编译期产物，运行时零作用 |
| **其余未挂载的 dsh 插件/工具** | 49 | 3.9 | `dsh-tool-cordis`、`dsh-terminal(-bash)`（终端控制器直接用 subprocess 起 shell，不用它）、`dsh-tool-*-persistent`、`dsh-session-log-export`、`dsh-time-context`、`dsh-persona`、`dsh-experimental-*`、`dsh-sdk-*` 等 |
| **合计** | **131** | **259.6** | |

---

## 四、B 档：保留包里还能刮掉的死文件（84.2 MB / 11 933 个）

在**不删任何包**的前提下删掉 `*.d.ts`、`*.map`、`*.md`、`*.pdb`。
node 不读这些；唯一影响是报错栈不指向 TS 源码、包里没 README。这一项零功能风险。

---

## 五、C 档：要先做产品决策的（本次没算进 218.8 MB）

| 项 | 体积 | 说明 |
|---|---|---|
| 多供应商 LLM SDK | 32.8 | `@google/genai` / `openai` / `@anthropic-ai/sdk` / `@aws-sdk/*`，由 `dsh-llm-pi-ai` 静态 import。**只走 DeepSeek 官方**的话可以连同 base 里的 `llm-pi-ai` 行一起下掉，再省 32.8 MB |
| `pnpm` | 23.0 | **不能删**：插件市场 `/dsh-market/install` 靠它装插件 |
| `@deepseek-ai/dsh-web-frontend` | 5.4 | 其中 `dist/` 5.4 MB 只有官方 Web UI 会读。删 `dist` 前请确认没人需要在浏览器里打开那个页面（`package.json` 必须留，`dsh-web-app` 启动时会 `require.resolve` 它） |
| `@opentelemetry/semantic-conventions` | 11.5 | 遥测用的常量表，`dsh-otel` 挂载着。除非连遥测一起关 |

---

## 六、千万别删（会被误判成孤立包）

| 包 | 体积 | 原因 |
|---|---|---|
| `@vscode/ripgrep-win32-x64` | 5.2 | `@vscode/ripgrep` 用拼接出的平台名 require，删了文件搜索直接挂 |
| `@img/sharp-win32-x64` | 18.9 | 同上，sharp 的图片处理后端 |
| `node-addon-require-builtin-win32-x64-msvc` / `@koromix/koffi-win32-x64` | 1.3 | 原生模块加载器 / FFI 后端 |
| `node-pty` | 26.8 | 右栏终端 + subprocess 的伪终端，被 `dsh-subprocess-local` 用 `createLazyRequire` 加载（字符串里带大写 `Require`，简单的 grep 会漏） |
| `pnpm` / `dsh-web-frontend` | 28.4 | 见 C 档 |

另外脚本还保守保留了 5 个 1 MB 以内的小包（`@opentelemetry/sdk-trace`、`@babel/runtime`、`cross-spawn`、
`default-browser`、`@earendil-works/pi-telemetry`）—— 判不出引用但体积太小，不值得为它冒启动失败的风险。
裁剪脚本里已经内置 `PROTECT` 白名单把它们全部锁死了。

---

## 七、怎么用

```bash
# 1) 干跑，看清要删什么
python misc/tools/prune-server-assets.py

# 2) 执行（默认移到 misc/prune-quarantine/<时间戳>/，可 --restore 还原）
python misc/tools/prune-server-assets.py --apply

# 3) 直接删（node_modules 本来就是 gitignore、可由 package-lock 重装）
python misc/tools/prune-server-assets.py --apply --purge

# 4) 只删整包、不刮包内死文件
python misc/tools/prune-server-assets.py --apply --no-junk
```

> `assets/resources/server/node_modules/` 不在 git 里（见 `.gitignore`），
> 重新 `npm install` 后清单会失效一点（版本若有变动），重跑一次 `audit-server-prune.py` 再裁剪即可。

## 八、裁剪后必验清单

1. 启动日志出现 `dsh web: http://127.0.0.1:xxxxx/?token=...` 与 `[DshApi] auth cookie acquired`（说明握手没坏）
2. 会话列表 / 新建会话 / 发一条消息（LLM 主链路）
3. 右栏终端开会话、敲命令（node-pty）
4. 文件树 + 全文搜索（ripgrep）
5. 拖一张图片进输入框（sharp）
6. 插件市场列表与安装（pnpm）
7. 工作区目录选择器（native picker）
8. 打开一个 docx/xlsx 看右栏预览（本地渲染，不该受影响 —— 若黑屏说明误删了 LibreOffice）
