# ULin Hub 插件界面平台：实现策略

> 2026-10-18。唯一口径。结论均有实测依据，来源见 §9。

---

## 1. 结论

1. **我们是客户端**：插件界面由我们原生实现，插件只提供数据与行为。
2. **主路径是协议 + 原生控件**；不内置、不启动网页版客户端。
3. 缺的只有三件事：**schema 表单渲染器**、**列表+动作渲染器**、**附件四件套**。

---

## 2. 架构

```
原生壳（QML/C++）       面板坞 · 设置区 · 仪表盘窗口 · composer
─────────────────────────────────────────────────────────
协议层（DSH mux）       settings/* · 插件自有 HTTP · 附件三件套 · session/*
─────────────────────────────────────────────────────────
插件（黑盒）            工具 · skills · 自有路由 · 宿主侧 settings
```

插件界面 = 它的数据面（自有路由或宿主 settings）+ 我们的原生控件。

---

## 3. 协议清单

| 能力 | 协议 | 状态 |
|---|---|---|
| 设置读取 | `settings/describe` → `{ writable, hasDocument, namespaces[] }`；每个 ns 带 `schema` / `value` / `user` / `revision` / `secrets` / `autoGenerate` | ✅ 已用 |
| 设置写入 | `settings/update { ns, patch, expectedRevision }`；`settings/mutate { ns, ops[{op:'set',path,value}], expectedRevision }` | ✅ 已用 |
| 设置变更 | `settings/document-updated(ns, revision)` | ⬜ |
| 插件数据面 | `GET/POST /plugins/<pkg>/…`（适配器 `api`） | ✅ 已用 |
| 上传附件 | `fileUploads/upload` | ⬜ |
| 提交消息 | `session/prompt`（`attachments` / `submittedAttachments`） | ⬜ |
| 读附件 | `session/attachment { sessionId, attachmentId }` → base64 | ⬜ |

核心依据：`autoGenerate: "Generate a page if no custom page is registered"` —— DSH 自己设计了"设置页自动生成"。
任何把配置放在宿主 settings 的插件，都能由 schema 生成原生表单。

---

## 4. 落点表

| 原版面（插件侧） | 我们的落点 | 形态 | 等级 |
|---|---|---|---|
| `settings.*` / `plugins.*` | 设置分区（`pluginUi.settings` → 设置页分区） | 容器 | L1 |
| `sidebar.right.pane.tab` | 块（`pluginData` / `pluginUi`） | 页签 | L1 |
| `sidebar.footer.action` | `sessionBar.extDock` | 位置器 | L2 |
| `shell.overlay` | `shell` | 自由 Item | L2 |
| `conversation.composer` | composer 卡片 | 输入面 | L1 |
| `conversation.chat.node` / `tool.call.*` | 无对应 | — | unsupported |
| 插件自 serve 页面 | `OpenWebWindow(url)` | 窗口 | L3（仅调试） |

等级：L0 数据可见 · L1 可读写 · L2 原生控件齐备 · L3 原版运行。

### 4.1 构造规范

| 类型 | 宿主 | 扩展 |
|---|---|---|
| 注入点 | QML 根 `Component.onCompleted: HostUi.registerItem(name, this)`；位置器（Row/Column）或自由 Item | `MountInto(owner, name, qml, ctx)`，owner 带 `/用途` 后缀 |
| 块 | `PanelBarDockModel` kind→视图；视图根 `Item`，内含注册名容器 | `open(kind)` + 往容器注入 |
| 设置分区 | 容器登记 `pluginUi.settings`；空则整块隐藏 | 注入内容；一分区一 owner |
| 窗口 | `OpenWindow(owner, qml, ctx, title)` / `OpenWebWindow(owner, url, title)` | 直接调用，owner 唯一 |
| 会话输入 | 规划：`AttachToComposer(owner, bytes, mediaType)` | 调它 |

契约：注册在 `Component.onCompleted`；扩展连 `namesChanged`（不用 0ms 定时器）；容器实例会变，变了整批重挂并复用桥；
扩展窗口随主窗口关闭。

---

## 5. 插件分类

### 5.1 判定规则（按序，命中即停）

| # | 条件 | 判定 |
|---|---|---|
| R1 | 含不可替代交互：图表/画布、富文本编辑、代码编辑器、视频 | 暂不映射（unsupported） |
| R2 | 去虚后 ≥3000 行 UI，或 ≥1200 行且组件 ≥8 | 延后（评估后定） |
| R3 | 含可替代交互：拖拽排序 → 上移/下移 | 原生映射，标注让步 |
| R4 | 其余（表单/列表/开关/按钮/对话框/toast） | 原生映射 |
| R5 | 无 `dsh.client` | 无需界面 |

### 5.2 去虚预处理（必做）

| 噪声 | 处理 |
|---|---|
| 同一套组件打两遍 | 按组件名出现次数除 |
| vendored 第三方库（日期选择、图标、CSS-in-JS） | 按组件名/来源剔除 |
| 关键字假阳性（`canvas` 在 CSS/类名里） | 只数 `<canvas` / `getContext(` |
| 真 vendored 富文本（codemirror/monaco） | 视为 R1 信号 |

### 5.3 样本结果（14 个插件）

| 类 | 数量 | 说明 |
|---|---|---|
| 无需界面（工具/skill/hook） | 3 | 模型直接用 |
| 设置型（宿主 settings） | 7 | schema 表单 |
| 路由型（自有 HTTP） | 5 | 列表+动作（与设置型重叠） |
| composer 卡片 | 1 | `plan-and-execute`（一张审阅卡） |
| 整页工作台 | 1 | `electro-lab`（去重后 ≈8.5k 行：tab + 3 列表 + 3 表单） |
| 需要网页渲染 | **0** | — |

控件覆盖：18 类中 12 类现成、5 类小补（单选/快捷键捕获/分页/图表/富文本）、1 类缺（拖拽 → 上下移替代）。
需补控件：`SwitchRow`、`SelectRow`、`RadioRow`、`ListRow`、`FormSection`、`BadgeChip`、`KeyCapture`、线性 `ProgressBar`。

---

## 6. 实现路线

| 优先级 | 工作 | 覆盖 | 依赖 |
|---|---|---|---|
| **P1** | schema 表单渲染器（describe → 控件；写回 update/mutate，带 `expectedRevision`） | 7 个设置型插件 | 已有 RPC |
| **P2** | 列表+动作渲染器（`api`：列表、行内按钮、分页/滚动） | 5 个路由型插件 | 已有 RPC |
| **P3** | 附件四件套（上传 → composer chip → 提交 → 消息内渲染） | 有产物的插件 | 3 个新 RPC |
| **P4** | composer 卡片（`plan-and-execute`） | 1 | P3 输入面 |
| **P5** | 整页工作台（`electro-lab`）或标 unsupported | 1 | 视工作量 |
| — | 网页窗口 | 仅调试/对照 | 已有 |

P1 是唯一"一次做全、多插件受益"的件，先做。

---

## 7. 适配器 schema

```jsonc
{ "sections": [{
  "plugin": "dsh-plugin-appshot",
  "slot": "settings.section",
  "title": "截图设置",
  "detail": "原位置：设置页分区",
  "api": "/plugins/appshot/config",      // GET 读 / POST 合并补丁写
  "installDir": "dsh-plugin-appshot",    // 相对 profile/node_modules
  "writable": true,
  "web": "",                             // 仅调试用
  "labels": { "soundEnabled": "提示音" },
  "values": { "shortcutMode#double-ctrl": "双击 Ctrl" }
}]}
```

| 字段 | 必填 | 缺省 |
|---|---|---|
| `plugin` / `title` | ✅ | 缺任一整条忽略 |
| `slot` | ✅ | 用于映射 |
| `api` | — | 空 = 无数据视图 |
| `installDir` | — | 空 = 不检测（靠 404 收敛） |
| `writable` | — | `false` |
| `web` | — | 空 = 无网页入口 |
| `labels` / `values` | — | 插件自带词条 → 内置词典 |

写回发原始 key / 原始 value，只有显示层翻译；类型保真（`true`/数字/JSON 不转字符串）。

---

## 8. 验收

| 项 | 通过标准 |
|---|---|
| 设置型 | 任取一个设置型插件：原生表单可读可写，写后读回一致 |
| 路由型 | 列表渲染 + 行内动作触发成功 |
| 附件 | 粘贴图片 → 发送 → 记录含 `attachmentId` → 消息内显示缩略图 |
| 隐藏 | 插件未装 / 数据面 404 → 落点消失，不留空面板 |
| 无特例 | `rg -i "<插件名>" source/` 只命中 `adapters.json` |
| 无网页 | 产品路径下不出现 `QtWebEngineProcess`（调试入口除外） |

---

## 9. 已否决路线（备查）

| 路线 | 否决原因 |
|---|---|
| 窗口打开官方 web 客户端 | 打开的是**整个客户端**，不是插件面板；web 客户端无深链（hash / URLSearchParams / history 均 0 命中），无法定位到某插件面 |
| 自建插件运行时（loader + React + `ctx`） | 等价重写官方客户端 ≈2.4 MB 代码 + 组件库 + 全部 `ctx` API |
| 无头 WebEngine 跑插件 JS | 可选非必需：附件与设置都已有协议 |

`OpenWebWindow` 保留为调试/对照能力，不进产品路径。

---

## 10. 证据索引

| 结论 | 出处 |
|---|---|
| 设置协议形状 | `@deepseek-ai/dsh-settings/lib/types/types.d.ts` |
| 设置 RPC | `@deepseek-ai/dsh-api-settings-controller`（`lib/typert.host.js`） |
| 我们已实现 settings RPC | `ModelSelectionService` / `AgentPresetService` / `SessionCommands` |
| 读附件 | `@deepseek-ai/dsh-api-session-controller`（`session.attachment`） |
| 上传 | `@deepseek-ai/dsh-client-file-upload`（`FileUploads`） |
| 图片渲染 | `@deepseek-ai/dsh-client-ui-conversation`（`readAttachment` → `data:` URL） |
| 插件 bundle 形态 | `window.__ModuleLoader__.load({id, factory})`；`require` 仅 react / react-dom / jsx-runtime / `@deepseek-ai/dsh-client-*` |
| 原版客户端启动页 | 挂 `@deepseek-ai/dsh-client-modules` 后 `/` 含 `__DSH_BOOT__` / `__ModuleLoader__` / `__DSH_BOOT_READY__` |
| web 客户端无深链 | `dsh-web-frontend`、`dsh-client-ui-sidebar-right` 扫描 |
| 样本数据 | `build/plan-eval/*.json` |

---

## 11. 大块 UI 插件怎么处理

**原则：不整页重画。** 大块 UI 只拆成落点单元，或只做摘要；功能由工具承担。

### 11.1 按"三个有没有"定档

| 判断 | 若"有" | 若"没有" |
|---|---|---|
| 配置在宿主 settings？ | schema 表单自动覆盖（P1） | 走自有路由 |
| 自有 HTTP 数据面（列表/状态）？ | 列表+动作（P2） | 只做摘要 |
| 功能已暴露为 tools？ | **界面缺失 ≠ 功能缺失**，可降级 | 界面是唯一入口，必须做 |

### 11.2 四档处理

| 档 | 做法 | 前提 |
|---|---|---|
| **拆分（首选）** | 拆成 1–3 个原生落点：设置表单 + 列表 + 关键动作按钮 | 现有控件够用 |
| **只读摘要（L0）** | 面板显示计数/状态 + "用工具操作"提示 | 有数据面但无写需求 |
| **unsupported** | 标注原因，功能走 tools（模型可直接调用） | 无可拆结构 |
| **调试窗口** | `OpenWebWindow`，仅开发者对照用 | — |

### 11.3 一条硬标准

大 UI 里若有**人类阻塞点**（必须等用户确认/输入才能继续，如计划审阅、危险操作确认），
**那个点必须原生实现**；其余部分可降级。否则流程会卡死。

### 11.4 两个样本的结论

| 插件 | 拆法 | 依据 |
|---|---|---|
| `dsh-electro-lab` | 设置表单（schema）+ 记录列表（只读，自有路由）+ 一个"生成"动作；**不做** `EditorDialog`（声明编辑器，面向作者） | tools：`external_solver_*`、`record_*`、`solver_info`；核心操作可由模型调用 |
| `dsh-plan-and-execute` | **必须做** composer 卡片（简化版：步骤列表 + 接受/修改 + 反馈框） | `submit_plan` / `report_step` 是入口，但审阅是**人类阻塞点** |

### 11.5 不做的事

- 不按像素复刻视觉；
- 不为单个插件写特性代码（差异只进 `adapters.json`）。

---

## 12. 完整性 vs 拆分：整体承载（plugin view）

拆分有代价：破坏 UI 的内部一致性（跨面板共享状态、联动、快捷键、连续工作流）。
所以对**不可分割的整体 UI**，提供第三种落点：**整体承载**——只跑这一个插件的 UI，不是整客户端。

### 12.1 判据：可拆 or 不可拆

| 特征 | 结论 |
|---|---|
| 各区块独立：设置项、独立列表、批量操作 | **可拆** → 原生落点（§4） |
| 跨区块共享本地状态 / 联动选中 / 同一编辑会话 / 实时预览 | **不可拆** → 整体承载 |
| 含画布、富文本、拖拽编排、时间线联动 | **不可拆** → 整体承载 |
| 人类阻塞点（确认/审阅） | 无论可拆与否，**该点必须原生** |

### 12.2 整体承载是什么（与"开官方客户端"的区别）

| | 整体承载（plugin view） | 已否决：开官方客户端 |
|---|---|---|
| 页面内容 | **只加载该插件的 bundle** | 整个 SPA + 所有插件 |
| 出现形式 | 我们面板里的一个视图 / 一个窗口 | 独立窗口里的另一个 DSH |
| 我们提供的 | loader（复用引擎已发的 client-modules）+ React + 组件库 + `ctx` shim | 无（引擎自带） |
| 完整性 | 插件 UI 原样渲染 | 原样，但带出整个客户端 |

### 12.3 成本清单（一次性，与插件数量无关）

| 件 | 来源 | 体量 |
|---|---|---|
| 模块 loader | 引擎已提供 `@deepseek-ai/dsh-client-modules/client.js`（40 KB，classic script，无 CORS 问题） | 0（复用） |
| React 运行时 | 需自带（React + react-dom + jsx-runtime，官方 vendor 块） | ≈723 KB |
| 官方组件库 | 需自带（`ui-slots` 22.9 KB + `ui-primitives` 518 KB，二者无独立 client 端点、在 SPA 里作静态模块） | ≈541 KB |
| `ctx` shim | 自写：`slots`（挂载到页面根）、`locale`（透传）、`sessions`/`conversation`/`attachments`（经 QWebChannel 桥回原生） | ≈300–500 行 JS |
| 宿主侧 | 一个 `WebEngineView` 面板 + QWebChannel 桥 | 小 |

插件 bundle 由引擎以 `plugins/??<pkg>/client.js` 提供（实测 200；classic script 不受 CORS 限制）。
`ctx` 的数据调用走原生桥（原生持有令牌），避免跨源与鉴权问题。

### 12.4 取舍

| | 原生拆分 | 整体承载 |
|---|---|---|
| 视觉一致性 | 高（跟随主题） | 低（原版 web 样式） |
| 完整性 | 受损（需重画） | 100% |
| 体积/内存 | 低 | +1.3 MB 运行时 + 每个视图一个渲染进程 |
| 插件迭代成本 | 界面变了要跟 | 零（插件自己渲染） |
| 适用 | 设置/列表/表单型 | 编辑器/工作台/画布型 |

### 12.5 混合是默认答案

外层原生（标题、动作按钮、状态、面包屑），内层整体承载（那个不可拆的工作区）。
`electro-lab` 属于这一类：壳与"生成"动作原生，工作区整体承载。

---

## 13. 整体承载的实测清单（2026-10-18）

下载并扫描 **25 个带 client 半的插件**（含新下载 10 个），原始数据 `build/whole-ui/`。

### 13.1 判据：看它"声请了什么"

| 信号 | 含义 | 结论 |
|---|---|---|
| **自建整块区域 slot**（如 `*.workspace`、`conversation.view`、`main`） | 插件要求一整块区域 | **整体承载** |
| 含编辑器 / 画布 / 真拖拽编排 | 不可廉价重建 | **整体承载** |
| 只用分散 slot（`settings.*`、`plugins.*`、`sidebar.right.pane.tab`、`conversation.composer`） | 可拆成落点单元 | **原生拆分** |

行数不是判据（会被 vendored 库与重复打包污染）。

### 13.2 结果

| 类 | 数量 | 插件 |
|---|---|---|
| **整体承载（确认）** | **1** | `@oh-story/dsh`：自建 `oh-story.workspace` 整块区域 + 三栏工作区（目录树 / 预览 / 序列），另用 `shell.overlay`、`tool.call.toolview` |
| 大块但分散（需评估：原生拆分 or 整体） | 5 | `dsh-electro-lab`（8.5k 行）、`dsh-plugin`（市场 7.4k）、`dsh-plugin-console`（33k，含 vendored 噪声）、`dsh-plugin-manager-companion`（9.2k）、`@laoyuehanni/dsh-git-worktree`（314 KB） |
| 可拆（分散 slot / 表单列表型） | 19 | 其余 |

### 13.3 两个误判纠正（说明为何不能只看关键词）

| 插件 | 表面信号 | 真相 |
|---|---|---|
| `dsh-client-ui-aqua` | `createElement("canvas")` + `getContext("2d")`（真代码） | 用于**生成 favicon/OG 图**，不在界面里；它的 UI 是纯设置项 → 可拆（且它改的是 web 样式，原生无对应） |
| `dsh-plugin-console` | `onDragStart` / `contentEditable` 命中 15 次 | 来自**打包进来的 react-dom 属性表**，不是自己的交互 → 假阳性 |

### 13.4 结论

> 需要整体承载的插件**很少**（本批 1/25）。绝大多数插件（24/25）用分散 slot，可以原生拆分；
> 剩下 5 个"大块但分散"的，按 §12.4 的取舍表逐个人工定档（先试拆分，拆不动再整体）。
> 判定顺序：**先看 slot 形态，再看交互范式，最后才看行数**。
