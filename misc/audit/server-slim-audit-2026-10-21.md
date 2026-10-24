# 服务端与资源精简审计（2026-10-21）

> 只读审计，未改动任何文件。所有体积为 `du` 磁盘占用，所有"用不到"结论都附代码证据。

## 0. 一句话结论

`assets/resources/server` 639M 里，**真正在运行期被加载的服务端逻辑合计约 10 MB**。其余 97% 是：一个从不挂载的 LibreOffice 引擎（184M）、63 个纯浏览器 UI 包（38M）、类型声明/sourcemap/README（~105M）、一个演示用截图插件（41M）、语音模型（23M）、以及 5 家不用的 LLM Provider SDK。

`x64/Release/resources/server` 已经是 **673M**（多出的 34M 是跑过之后留下的运行期产物，含 `.credentials.yaml`）。

## 1. 客户端实际加载边界（判定的基准）

启动链（唯一一条）：

```
source/src/core/ServerManager.cpp:1001
  <exe>/resources/server/node.exe
  <exe>/resources/node/harness-node-entry.mjs
  <exe>/resources/server/node_modules/@deepseek-ai/dsh/lib/bin.js
  web --port 0
```

- argv 只有 `web --port 0`：无 `--profile` / `--host` / `-c` / `--patch`。
- `DSH_HOME` = `<exe>/resources/server/harness`（`ServerManager.cpp:943`），profile 固定 `profiles/web`（硬编码 6 处）。
- profile 的 `package.json` 里 `dsh.profile.bundles = ["@deepseek-ai/dsh-base"]`。
  → 只有 `dsh-base` 与 `profiles/web` 两份 `cordis.patch.yml` 会被应用（`dsh-app-boot/lib/index.js:468-508`）。
- `harness/profiles/web/cordis.patch.yml:1` 自述："Minimal API-only profile for the ULin Hub Qt client"。
- 客户端只请求 `/api/*`（`DshApiClient.cpp:280` WS mux、`:531` POST）与插件自定义路由，**从不 GET `/`，不加载任何前端静态资源**。

`dsh-base/cordis.patch.yml`（529 行）里关键词命中数：

| 关键词 | 命中 | 含义 |
|---|---|---|
| `client-ui` | **0** | 53 个浏览器 UI 包一个都不挂载 |
| `office` | **0** | LibreOffice / office→PDF 整条链未挂载 |
| `webhook` | **0** | GitHub Webhook 未挂载 |
| `acp` | **0** | ACP/SDK 模式未挂载 |
| `speech` | **0** | 语音转写未挂载 |
| `otel` | **6** | 遥测**默认开启**（第 188/204 行） |

远程方法面：服务端共 140 个 `typert` 远程方法，客户端只调 33 个，**107 个零调用**（账号登录、语音、图像端点、遥测上报、Agent Team、Git 集成、goals、schedule、officeToPdf、workspaceFiles、fileUploads、pluginManager 等）。

---

## 2. 体积排行榜与判定

### Tier A — 零风险，不需改任何配置或代码

| # | 目标 | 体积 | 判定 | 证据 |
|---|---|---|---|---|
| A1 | `node_modules/@deepseek-ai/libreoffice-kit-win32-x64` | **184M** | 删 | 两份 patch 里 `office` 命中 0；唯一消费者 `dsh-office-to-pdf` / `dsh-skill-office` 均未挂载；引擎由 `libreoffice-kit/lib/index.js` 在函数体内 `require.resolve` 惰性解析，不 import 不报错 |
| A2 | `harness/profiles/web/node_modules/dsh-plugin-appshot` | **41M** | 删（同时删 patch 里那 6 行） | `cordis.patch.yml` 自注"演示：第三方插件"；`source/` 全目录 grep `appshot` 0 命中；其中 41,731,568 B 是 `appshot-win-x64.exe`，且被 `.gitignore:20 (*.exe)` 排除，非受控资产 |
| A3 | `node_modules/**/*.map` | **48M**（4344 个） | 删 | 服务端不读 sourcemap |
| A4 | `node_modules/**/*.d.ts` | **47M**（7334 个） | 删 | 运行期零加载；`lib/*.js` 中 `.d.ts` 引用 0 命中 |
| A5 | `node_modules/**/*.md`、`LICENSE*`、`CHANGELOG*` | **11M**（966 个 md） | 删（保留 MPL 系 `licenses/`） | 运行期零作用 |
| A6 | 各包内 `test/ tests/ __tests__/ docs/ examples/` | **~14M** | 删 | 无运行时引用；大头是 `@mixmark-io/domino/test` 7.2M（其 `lib/` 仅 659K） |
| A7 | 53 个 `@deepseek-ai/dsh-client-ui-*`（除 A8 两个） | **~31.9M** | 删 | 未进加载闭包；大件 `sidebar-documentpreview` 15M（pdf.js/ExcelJS）、`settings-account` 5.3M |
| A8 | ⚠️ 反向：`dsh-client-ui-directory-picker-native` / `-browse` | 144K | **必须保留** | `dsh-host-directory-picker-auto/lib/index.js:107-108,138` 启动时 `ctx.loader.create()` 动态挂载，失败直接 throw |
| A9 | `node_modules/@deepseek-ai/dsh-web-frontend/dist` | **5.8M** | 删，**保留 package.json** | `dsh-web-app/lib/index.js:176` 在 `apply()` 里调 `resolveDistIndex()`；`:110` 只 `require.resolve(".../package.json")`，`:111` 注释明写"dist 存在性是请求期的事"。整目录删会 boot 失败 |
| A10 | `sherpa-onnx-win-x64` + `sherpa-onnx-node` | **23M** | 删 | 唯一使用者 `dsh-experimental-speech-to-text-sensevoice/lib/worker.js:74`；该链未挂载 |
| A11 | `@octokit/*` | **8.5M** | 删 | 唯一引用 `dsh-webhook-github/lib/index.js:3`，未挂载；其中 8.1M 是 `.d.ts` |
| A12 | `@modelcontextprotocol/*` | **7.8M** | 删 | 唯一引用 `dsh-mcp-client`，后者只被 `dsh-acp` 用；活跃的 `dsh-mcp-resources` 不依赖它 |
| A13 | `@agentclientprotocol/sdk` | **5.8M** | 删 | `dsh-acp/lib/index.js:9`，同属未挂载链 |
| A14 | `@js-temporal/polyfill` + `dsh-schedule` + `experimental-schedule-bundle` | **3.6M** | 删 | base patch 无 schedule 行（唯一命中是注释） |
| A15 | `web-streams-polyfill` + `node-fetch` + `fetch-blob` + `formdata-polyfill` | **9.0M** | 删 | 全仓运行时无 `require("node-fetch")`；Node 22 自带全局 fetch |
| A16 | `fontkit` + `libreoffice-kit` + `dsh-office-to-pdf` + `dsh-skill-office` | **7.2M** | 删（随 A1） | 仅 office 链使用 |
| A17 | `server/package-lock.0.1.5-rc.2.json`、`0.1.7-rc.2.json` | 650K | 删（死文件） | 全仓 grep 零引用；git 跟踪属初始提交带入 |
| A18 | `server/launch-root/ppt-test/` | 864K | 清空内容，留空目录 | 全是 PPT 测试产物；`ServerManager.cpp:571` 会 `mkpath` 建目录；全仓只有 1 处注释提到 |
| A19 | `harness/profiles/web/*.before-appshot`（2 个） | 2.7K | 删 | 编辑器临时备份 |
| A20 | `harness/profiles/web/node_modules/@deepseek-ai/dsh-api-terminal-controller` | 387K | 删（先冒烟） | 与顶层同版本同 md5，纯重复；Node 向上查找可回落到 `server/node_modules` |
| A21 | `harness/profiles/web/node_modules/ToolsFilterPlugin` | 50K | 删 | 运行期由 `ServerManager.cpp:882-905` 从 `:/ULinHub/ToolsFilterPlugin` 重装 |

**Tier A 合计 ≈ 400 MB**（node_modules 511→约 185M；harness 41→约 0.4M）。

### Tier B — 需先改 profile 配置（否则起不来）

| # | 目标 | 体积 | 前置动作 |
|---|---|---|---|
| B1 | `@opentelemetry/*` | **31M** | `dsh-base/cordis.patch.yml:188 (otel)`、`:204 (session-telemetry-otel)` **默认启用**，且在 `dsh-otel/package.json` 里是硬 dependencies。必须先在自己 profile 里补 `- id: otel` / `- id: session-telemetry-otel` + `disabled: true`，再删包。否则 loader 抛 `ERR_MODULE_NOT_FOUND`，整个服务端起不来 |

> 顺带：`:211` 的 OTLP endpoint 硬编码上报到 `https://dsh-otel-collector.deepseeksvc.com/v1/logs`，而客户端对 `telemetry` / `analytics` 零调用。**关掉它既有体积收益，也去掉一条默认外联**。

### Tier C — 产品取舍（不是死代码，是"我们不打算支持"）

| # | 目标 | 体积 | 取舍点 |
|---|---|---|---|
| C1 | `openai` 15M + `@anthropic-ai/sdk` 12M + `@google/genai` 11M + `@aws-sdk`/`@smithy` 11.2M + `protobufjs` 3.1M | **~52M** | 全是 `@earendil-works/pi-ai/providers/*.lazy.js` 里的**动态 import**，删了不影响启动。但客户端设置页有 `llm/listConfigurableProviders` + `ConfigurableProvider`，删了对应 Provider 选项即失效。**若产品定位 DeepSeek-only（+ Codex 接管），这 52M 可以全部拿掉** |
| C2 | `@img/sharp-win32-x64` 19M + `sharp` | 20M | `dsh-attachment-local/lib/index.js:118` 懒加载，只在上传图片附件时用。客户端目前是内联 base64 直送 `session/prompt`，理论上不触发；但**一旦支持"拖入图片存盘"就会炸**，属最难排查的回归。建议保留 |
| C3 | `node-pty` | 27M | **必须保留**。`dsh-subprocess-local` 的终端实现全是它，`/api/terminal/*` 缺一行就 404 |
| C4 | `node.exe` | 88M | 单独一块。可换 small-icu / 精简构建，或用安装包 lzma 压缩（88M → 约 25M 分发体积） |

### 不可动名单（看着像 UI / 像遗留，其实是核心）

1. `@deepseek-ai/dsh-web-app`（132K）—— 名字最像 Web UI，实际是 `/api` 认证 cookie 的铸造者。
2. `@deepseek-ai/dsh-host-frontend-static`（40K）—— 被 `dsh-web-app/lib/index.js:8` 静态 import。
3. `dsh-client-connection` / `dsh-client-file-upload` / `dsh-client-store`（449K）—— 前缀 `client-`，实为 host 侧服务。
4. `dsh-session-format-v0-to-v1` … `v3-to-v4` —— 看着像历史迁移代码，被 `dsh-session-format-catalog` 静态 import。
5. `dsh-api-remotes`（593K）—— `/api` 协议本体，`lib/client.js` 内联了全部 remote 定义。
6. `@vscode/ripgrep`（5.2M）、`@xterm/headless`（2.2M）、`koffi`（1.9M）、`zod`/`typebox`/`ajv`/`undici`/`turndown` —— 均为活跃包依赖。

---

## 3. 执行方式的两个坑（不做这两件事，删了也会长回来）

1. **`package-lock.json` 仍记录被删的包**。`PluginMarketInstaller` 会对 profile 目录跑 npm/pnpm install，下次插件安装或 `npm ci` 会把它们原样装回。
   → 裁剪必须做成**打包后的 prune 步骤**（维护一份 `prune-manifest.json` + 脚本），或另存一份裁剪过的 lock。
2. **`harness/profiles/web/node_modules` 是手工放的，没有 lockfile**（`pnpm-workspace.yaml` 只有 `packages: [.]`，profile `package.json` 的 `dependencies` 是空 `{}`）。删掉的 4 个包里 appshot 无法重建，删前确认没有回滚需求。

---

## 4. 非体积问题（本轮顺带发现，优先级可能更高）

| 严重度 | 问题 | 证据 |
|---|---|---|
| **高** | 发布目录里带 **`.credentials.yaml`**、`.anonymous-user-id`、`sessions/`(117K)、`storages/`(84K)、`.desktop-bin/` | `x64/Release/resources/server/harness/` 实测存在。若打安装包时整目录拷贝，**用户 API Key 会随包分发** |
| **高** | 发布副本与仓库种子不同步：仓库 `profiles/web/package.json` 的 `dependencies` 是 `{}`，发布副本是 `{"dshmarket":"1.66.32","dsh-plugin-appshot":"0.4.2"}`；仓库里**没有 `dshmarket` 包** | `PluginMarketController.cpp:308-314`、`ServerManager.cpp:788-796` 都期望它存在 → 干净安装下插件市场只会 404 |
| 中 | profile patch 写的是 `@deepseek-ai/dsh-agent-presets`（复数），实际包名是 `dsh-agent-preset`（单数） | 该行解析不到包，被 fail-soft 静默跳过 → 默认预设配置实际未生效 |
| 中 | `.qm` 比 `.ts` 旧一轮，**311 个 id 缺 6 个**，而这 6 个都在出货代码里用了 | `.qm` 10-19 / `.ts` 10-20；缺 `workdir_exists_fmt`、`chat_turn_done`、`chat_process_think_*`、`chat_process_tools_*` → 界面直接显示 id。修法：重跑 `misc/tools/release-translations.ps1` |
| 低 | 发布目录 `profiles/web` 51M vs 仓库 42M，多出 9M 是 pnpm 实装的 `dshmarket`(5M)/`react`/`undici`/`pnpm-lock.yaml` 等 | 属"跑过之后"的状态，不应进包 |
| 低 | `apps/Style Example`（1.35M，`wallpaper.png` 1.4M）是纯设计示例皮肤 | 不在任何 .qrc；`Backdrop.qml:9` 里那条引用路径根本不存在（`StyleExample` 无空格） |
| 低 | `assets/resources/icons/` 里 `attach-icon.png` / `-48` / `-96` 三个文件未入 qrc 且全仓零引用 | 只有生成脚本 `make-attach-icon.py` 提到 |
| 低 | `assets/resources/docs/`（97K，4 个 md）不进 exe、无运行时引用 | 建议移出 `assets/`，否则发布脚本整目录拷贝会白带 |

> Qt 侧结论：**qrc 里的 21 条资源 100% 有引用点**（styles 2/2、buttons 4/4、icons 9/9、qm 2/2、ToolsFilterPlugin 3/3、config 1/1），没有"打包了但零引用"的死资源。死重量都在**未打包的仓库资产**里。

---

## 5. 「未来也用不上」的判定（与路线图无关，可无条件删）

区分两类：**路线图依赖**（今天不用，将来可能用 → 要决策）vs **结构决定**（无论做什么都用不到 → 可永久删）。

结构性论据（已核实）：

1. **只发 Windows**：`source/CMake/CMakeLists.txt:124` `qt_add_executable(ulinhub WIN32)`，全文只有 `if(WIN32)` 分支，无 `APPLE`/`UNIX`；`misc/tools/` 无 mac/linux 打包脚本。node_modules 里也只装了 `*-win32-*` 平台包。
2. **只有一条启动路径**：`web --port 0`（`ServerManager.cpp:1001`）。TUI / daemon / ACP / stdio-SDK 模式在客户端不存在分支（`DSH_SERVER_URL` 接管外部服务端时走的仍是 HTTP+WS）。
3. **Qt 是唯一 UI**：`openBrowser: false` 写死在 profile；客户端从不 GET `/`；README 明确 UI 路线是 Qt Widgets → QML，不回退浏览器。插件 UI 由宿主用 QML 渲染（`kKindPluginData` 等块），不加载前端 bundle。
4. **服务端绑 `127.0.0.1` + 随机端口 + 一次性 token**：GitHub Webhook 这类需要公网回调的能力结构上不可能成立。
5. **Node 22 自带全局 fetch**：polyfill 链永不需要。

| 类别 | 项 | 体积 | 为何永久无用 |
|---|---|---|---|
| 打包垃圾 | `*.map` 4344 个 / `*.d.ts` 7334 个 / `*.md` 966 个 / 各包 `test,docs,examples` | **~105M** | 运行期零加载，与产品无关 |
| 浏览器前端 | 53 个 `dsh-client-ui-*`（32M）+ `dsh-web-frontend/dist`（5.8M） | **38M** | 论据 3。唯一残余路径是插件调 `HostUi` → `openUrl` 把服务端首页塞进坞里，等于在自家窗口嵌别人的控制台，产品上不成立 |
| 演示插件 | `dsh-plugin-appshot`（含 `native/macos/` 348K） | **41M** | 第三方示例，patch 自注"演示"；要截图我们不会用它的实现 |
| 死协议链 | `@modelcontextprotocol` + `@agentclientprotocol` + `dsh-acp*` | **~14M** | 论据 2 |
| 平台不可能 | `@octokit/*` + `dsh-webhook-github` | **8.6M** | 论据 4 |
| 运行时已内置 | `node-fetch` / `web-streams-polyfill` / `fetch-blob` / `formdata-polyfill` | **9M** | 论据 5 |
| 死文件/重复 | 两个旧 `package-lock`、`.before-appshot` ×2、`api-terminal-controller` 重复副本、`launch-root/ppt-test` | **~1.9M** | 零引用 |

**合计约 200M**（垃圾文件与死链包有重叠，已按不重复计）。这一档不需要任何产品决策。

## 6. 建议的推进顺序

1. **先摘 Tier A 的 A2（appshot 41M）+ A1（libreoffice 184M）** —— 两项合计 225M，占可省总量一半，且都不碰配置。
2. 再跑一遍 `npm/prune` 式清理（A3–A7、A9），配一个 prune 清单脚本固化，避免回到 511M。
3. 单独一版改 profile 关掉 otel（B1），顺手解决默认外联。
4. C1 等产品明确"只支持哪些 Provider"后再动。
5. 先把第 4 节的 `.credentials.yaml` 打包排除加进发布脚本 —— 这条与"精简"无关，但风险最高。
