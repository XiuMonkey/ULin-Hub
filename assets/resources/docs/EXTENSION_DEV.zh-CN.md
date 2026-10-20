# ULin Hub 扩展（.ext）开发文档

本文档面向希望为 **ULin Hub**（Qt 6 / C++ 桌面客户端 + 本地 DSH 服务端）开发自定义扩展的开发者。

扩展 = 一个 `.ext` 文件（ZIP 格式）。它把三样东西打包在一起：

1. **原生工具逻辑**：一个或多个 DLL（`main.dll` 及可选依赖）；
2. **工具描述**：`regulation.json5`（声明每个 DLL 导出函数如何映射成一个“DSH 工具”）；
3. **可选的 DSH 插件**：`AttachedPlugin/`（`package.json` + `index.js`），在 DSH 服务端注册工具让 Agent 能用；有**两种模板**——手写 `index.js` 逐工具注册，或直接用**通用注册器 + tools.json**（加工具只改 JSON，见 §6.2）。

> 阅读前提：熟悉 JSON/JSON5、Windows DLL 导出、DSH（DeepSeek Harness）插件的 `defineTool` 约定。

---

## 1. 目录结构与打包

推荐布局：

```text
MyExtension.ext            （zip 打包）
├── main.dll               主原生 DLL（必需）
├── regulation.json5       工具描述（必需）
├── AttachedPlugin/        DSH 插件（推荐，工具才能被 Agent 调用）
│   ├── package.json
│   └── index.js
└── bin/                   （可选）运行时依赖（ffmpeg 等第三方 DLL/可执行文件）
```

打包为 `.ext`：

- 压缩包**根目录**必须直接包含 `regulation.json5` 与 `main.dll`（程序支持任意层级查找，但建议放根）；
- 兼容 `\` 与 `/` 两种路径分隔（`Compress-Archive` 等工具产出的反斜杠路径可用）；
- `package.json` / JSON5 里**不要带 UTF-8 BOM**（安装时程序会自动剥离，但最好一开始就没有）。

仓库内可直接参考的样例：

- `test extension/NativeThunkExt/`：native（Thunk）DLL 工具 + **通用注册器模板**（`AttachedPlugin/tools.json`）——新扩展建议照它做；
- `test extension/NativeThunkExt/AttachedPlugin/index.js`：通用加载器本体（可原样复用）；
- （历史样例 FFmpegExt / PPTExt 已不在源码树，仅存于运行目录快照，参考价值有限。）

---

## 2. regulation.json5 —— 工具描述

示例（json 风格）：

```json5
{
  "Name": "FFmpegTools",
  "Description": "FFmpeg media processing tools",
  "Function": [
    {
      "Func": "ffmpeg_probe",
      "LoadingSource": "main.dll",          // 从哪个 DLL 导出（相对描述文件目录）
      "InterfaceType": "default",           // default(默认)/com
      "Calling Convention": {
        "Style": "json",                    // json | native
        "ReturnType": "string",             // string | int | void（json 风格）
        "ResultIsJson": true                // 返回值是否是 JSON 文本
      },
      "Tool": "ffmpeg_probe"                // DSH 工具名（全局唯一）
    }
  ]
}
```

字段说明：

| 字段 | 说明 |
|---|---|
| `Function[].Func` | DLL 导出函数名（`default` 接口必填） |
| `Function[].Tool` | 暴露给 Agent 的工具名；**在整个应用里必须唯一**（多扩展同名会被拒绝加载） |
| `Function[].LoadingSource` | 函数来自哪个 DLL，相对 `regulation.json5` 所在目录解析；同一扩展可从不同 DLL 导出多个工具；缺省 `main.dll` |
| `Function[].InterfaceType` | `default`＝原生 DLL；`com`＝走 COM 自动化（见 §5） |
| `Calling Convention.Style` | `json`：统一桥接签名；`native`：Thunk 按真实 C 签名调用 |
| `Calling Convention.ReturnType` | json: `string/int/void`；native 支持 `void/bool/double/string(const char*)/int` |
| `Calling Convention.ResultIsJson` | 返回内容是否按 JSON 解析 |

---

## 3. json 风格 —— DLL 桥接签名

DLL 按以下三种签名导出（参数一律传 JSON 文本）：

```c
// 方式一：const char* Func(const char* argsJson)
const char* ffmpeg_probe(const char* argsJson);

// 方式二（返回 int 表示错误码，0=成功）：
int    Func(const char* argsJson, char** resultJson);
// 或方式二（void 变体）：
void   Func(const char* argsJson, char** resultJson);
```

### 结果内存契约（ClearMem）

- **新约定**：若 DLL 导出了 `void ClearMem(const char* p)`，说明 json 风格（`ReturnType=string`）返回的是 DLL `malloc` 的堆内存；Qt 端读取**拷走后立即调用 `ClearMem` 归还**。
- **老扩展**：没有 `ClearMem` 导出时，保持“static 缓冲、拷走即用、不释放”的兼容行为。

因此：新 DLL 建议统一用“分配字符串 → 导出 `ClearMem`”的方式；若用 static 缓冲则不要导出 `ClearMem`，也不要并发重入同一函数（§8）。

`ResultIsJson=true` 时返回内容必须是合法的 JSON **对象**。

---

## 4. native 风格 —— Thunk 真实签名

若希望按 DLL 真实 C 签名调用（避免来回 JSON 序列化），声明：

```json5
"Calling Convention": {
  "Style": "native",
  "ReturnType": "int"
},
"Parameters": [
  { "Name": "a", "Type": "int" },
  { "Name": "b", "Type": "double" },
  { "Name": "s", "Type": "string" }
]
```

支持的参数/返回类型：`bool / int / double / string(const char* / char*) / pointer32 / pointer64`（参数数量上限 16）。

Qt 端用 Thunk 生成 x86-64 适配层调用，参数由 JSON 提供、缺省值取 `Parameters[].Default`。

---

## 5. com 接口（InterfaceType = com）

用于调用 Windows COM 自动化（IDispatch）。组件 **ProgId 白名单式声明**在描述里：

```json5
{
  "Func": "",
  "InterfaceType": "com",
  "Com": { "ProgId": "My.Calc" },
  "Tool": "com_my_calc"
}
```

运行时入参（来自 Agent 的 JSON）：

```json
{
  "member": "Add",          // 方法或属性名
  "kind":   "method",       // method | get | put
  "path":   ["PropA"],      // 可选：先沿属性链下钻
  "params": [1, 2],         // kind=method 的参数（仅 JSON 标量）
  "value":  42              // kind=put 的值
}
```

- 每次调用新建组件实例（无状态、无句柄保活），可跨线程并行（内部按线程初始化 COM）；
- 返回对象时只给摘要（`Count`），不支持下钻保活。

---

## 6. AttachedPlugin —— 让 Agent 能用到这些工具（两种模板）

### 6.1 手写 index.js（灵活模板）

光有 DLL + 描述还不够：Agent（模型）只认 DSH 服务端插件注册的工具。因此把工具注册脚本放进扩展：

```js
// AttachedPlugin/index.js
import { defineTool } from "@deepseek-ai/dsh-tools";

export const name = "MyExtensionPlugin";
export const inject = ["tools"];

export function apply(ctx) {
  ctx.tools.register(defineTool({
    name: "my_tool",                    // 必须与 regulation 里 Tool 一致
    description: "…",
    parameters: { /* 与 native/json 约定呼应 */ },
    isConcurrencySafe: () => false,     // 保守建议 false
    async execute(args) {
      return bridge.call("my_tool", args); // 见下：命名管道桥接
    }
  }));
}
```

### 6.2 通用注册器 + tools.json（推荐：加工具只改 JSON）

如果每个工具都只是“参数 → 命名管道 → Qt 调用 DLL”，**不需要手写注册 JS**：把仓库自带的**通用加载器**（`test extension/NativeThunkExt/AttachedPlugin/index.js`）原样放进你的 `AttachedPlugin/`，它会读取同目录的 `tools.json`，逐个注册工具并桥接到 Qt 端。

```text
AttachedPlugin/
├── package.json    // name/main 与示例一致（"type":"module", main: "index.js"）
├── index.js        // 通用加载器（复制示例即可，不用改）
└── tools.json      // { "tools": [ 工具定义... ] }
```

**tools.json 格式**（完整样例：`test extension/NativeThunkExt/AttachedPlugin/tools.json`）：

```json
{
  "tools": [
    {
      "name": "native_add3",             // DSH 工具名（全局唯一）
      "description": "Add three integers…",
      "bridgeTool": "native_add3",       // Qt 端 regulation 里的 Tool 名（缺省 = name）
      "concurrencySafe": false,          // false=Qt 同 DLL 串行；确认线程安全再开
      "parameters": {                    // JSON-Schema 子集，供模型填参
        "a": { "type": "integer", "required": true, "description": "…" },
        "b": { "type": "number",  "required": true, "description": "…" }
      },
      "output": { "type": "object", "additionalProperties": true }
    }
  ]
}
```

- `parameters` 的 `type` 支持 `integer / number / string / boolean / object / array`，每个字段可带 `required` 与 `description`；
- 通用加载器对每个定义执行等价逻辑：`defineTool({ name, description, parameters, output, isConcurrencySafe: () => !!def.concurrencySafe, async execute(args){ return bridge.call(def.bridgeTool || def.name, args); } })`；
- 加载时打印 `loaded tools.json: …` 与 `register tool: …`（走服务端日志），方便确认。

**新增一个工具 = 三步，不碰 JS**：

1. `regulation.json5` 加一条 `Function`（`Func` = DLL 导出名，`Tool` = tools.json 的 `bridgeTool`/`name`）；
2. DLL 实现并导出该函数（json / native 风格，见 §3 / §4）；
3. `tools.json` 的 `tools` 数组加对应定义。

> 通用加载器已统一处理：管道自动重连、30s 超时、错误 reject、插件 dispose 时关闭管道。手写插件需要同样能力可直接复用。

---

### 命名管道桥接

扩展 DLL 跑在 **Qt 客户端进程**里；DSH 插件跑在 **Node 服务端进程**里。两者之间用命名管道：

- 管道名：`\\.\pipe\ulinhub-bridge`；
- 协议：**每行一个 JSON**；
  - 请求：`{"id":1,"tool":"my_tool","args":{...}}`
  - 响应：`{"id":1,"ok":true,"result":{...}}` 或 `{"id":1,"ok":false,"error":"..."}`；
- Qt 端收到请求后，按 `tool` 名在已加载扩展描述里查函数并调用（同 DLL 串行、跨 DLL 并行，见 §8），结果回写管道。

可参考 `test extension/NativeThunkExt/AttachedPlugin/index.js` 的桥接实现（自动重连 + 30s 超时）。

> 注意：Node 端请求是异步多发的，响应可能乱序返回——**按请求 `id` 配对**，不要假设顺序。


## 7. 安装 / 卸载 / 重装行为

安装一个 `.ext` 后，程序会：

1. 解压到临时目录并解析 `regulation.json5`；
2. 把运行文件持久化到 `harness/profiles/web/extensions/<插件名>/`；
3. 若带 `AttachedPlugin`，把插件目录复制到 `…/node_modules/<插件名>/`，并向 `cordis.patch.yml` 追加一行启用；
4. 更新 `extensions.json` 注册表（UI 列表的来源）；
5. **重启 DSH 服务端**，插件 `apply()` 注册工具；
6. Qt 侧 `DllCaller` 追加加载该扩展描述（支持多扩展并存，互不覆盖）。

约定与限制：

- **工具名全局唯一**：Qt 侧与 DSH 插件注册侧都对“同名工具”拒绝（会报错并提示先卸载冲突方）；
- **重装同一扩展**：请先在“扩展管理”里**移除**再安装（Qt 会先按名卸载该扩展的 DLL、释放文件占用；否则覆盖/注册会被拒）；
- 移除扩展后服务端会自动重启，工具随之消失；
- 扩展目录名取自 `package.json name`（无插件时为解压目录名）——请用合法的目录名字符，不要含路径分隔符。

---

## 8. 并发与线程模型（Qt 侧）

- 工具请求在 **GUI 线程之外的工作线程池**执行（GUI 不会被长任务卡住）；
- **同一个 DLL 内部串行**（`runMutex`）：老扩展可能是 static 缓冲/非重入实现，跨线程并发不安全；**不同 DLL / 不同扩展并行**；
- `com` 每次调用独立初始化（MTA），可并行；
- 卸载/移除扩展会**等待该 DLL 上在途调用结束**再卸载，不会卸载使用中的库；
- 因此：json 风格 + `ClearMem` 的实现，理论上可放开同库并发；若确实线程安全，可在插件侧说明并考虑放开，但默认按串行更稳。

---

## 9. 调试与常见问题

日志关键字（见 `x64/Debug/logs/`，按模块分目录，如 `logs/session/info.log`、`logs/extension/error.log`，模块内再按级别分为 `debug.log` / `info.log` / `warn.log` / `error.log` / `fatal.log`）：

- `[DllCaller] descriptor loaded / library loaded / tool call succeeded / unknown tool …`
- `[DSH Pipe] request / response`
- `[Market] …` / `[dsh-market-log] …`（插件市场，扩展一般用不到）
- `[DSH Server] …`

排查路径：

1. **模型说找不到工具**：先确认 AttachedPlugin 已随扩展安装并触发过服务端重启；再确认 `cordis.patch.yml` 里插件行存在；
2. **调用报 `unknown tool`**：Qt 侧描述未加载/被同名冲突拒绝——看 `[DllCaller] descriptor loaded` 是否出现该扩展；
3. **调用报 DLL/管道错误**：检查 `bin/` 依赖是否齐全、DLL 是否导出 `ClearMem`（与实现匹配）、管道 30s 超时（长任务请拆小或调大 Node 端超时）；
4. **工具注册冲突**：工具名重复，先卸载旧扩展；
5. **git 类 DSH 插件安装失败（市场）**：与本格式无关，属 dshmarket/pnpm 链路（Qt 已为服务端注入 pnpm PATH，若仍失败看 `[dsh-market-log]`）。

---

## 10. 最佳实践清单

- 工具名全局唯一、语义化（如 `myext_action`）；
- **优先用通用注册器模板**（§6.2）：加工具只改 `tools.json` 与 `regulation.json5`，别复制注册样板；
- DLL 用 `ClearMem` 契约 + 每次调用独立分配/释放，避免 static 缓冲跨线程；
- 参数只用 JSON 标量与对象；返回尽量是 JSON 对象（`ResultIsJson:true`）；
- 长耗时任务要有进度/超时策略（管道侧单次请求超时默认 30s）；
- 依赖 DLL 放 `bin/`，并在 `LoadingSource` 中引用相对路径；
- 一个 `.ext` 只放一套逻辑；需要“多 DLL 导出工具”时用多个 `LoadingSource` 描述同一扩展即可；
- 发布前跑一遍：安装 → 移除 → 重装 → 暗色/亮色主题下调用。

---



---

## 11. 客户端扩展（`Type = ClientExtension`）：DLL + QML 两个扩展系统

前面 1~10 节讲的都是**工具扩展**（装进服务端 profile、给 Agent 加工具）。本节讲另一条线：
**客户端扩展** —— 装进宿主进程 `<exe>/clientExtensions/<Name>/`，可以带原生 DLL、也可以带 QML。

> ⚠️ **本节分"现状"与"目标形态"**。QML 那一半（扩展 QML 的装载与注入：
> `HostUi` / `VirtualUiHost`）**已于 2026-10-01 实现**（§11.3 就是实现后的形态）。11.1 表里"扩展能拿到的宿主对象"那句仍是旧的：现在可挂对象是 `shell` / `titleBar` / `topBar` / `sessionBar` / `sessionBar.extDock` / `composer`。

### 11.1 现状（已实现，可直接用）

| 能力 | 实现 | 说明 |
|---|---|---|
| 安装 | `ExtensionLoader::loadAndInstall` | 解压到临时目录 → 按 `regulation.json5` 的 `Type` 分流 → 客户端扩展**整包落地**到 `<exe>/clientExtensions/<Name>/`（dll、regulation 之外的 **QML 与资源也一并拷过去**） |
| DLL 装载 | `ClientExtension::loadAll()` / `loadOne()` | 启动时（`engine.load()` 之后）扫目录：没有 `regulation.json5` 的跳过；`main.dll` 优先，否则取目录里第一个 `*.dll`；`QPluginLoader` → `qobject_cast<DshHostPlugin*>` → `setHostIdentity(目录名)` → `attachHost()`。全程 **GUI 线程** |
| 卸载 | `ClientExtension::remove()` | 先删判据（`regulation.json5`）⇒ 不再算已安装；dll 被占用则留残留，下次启动 `sweepPendingRemovals()` 清 |
| 管理界面 | `Hub.extensions`（`ExtensionManagerController` + `basic/extensions/ExtensionManagerPage.qml`） | 两种来源：工具扩展（`profiles/web/extensions.json`）、客户端扩展（`<exe>/clientExtensions`） |
| 扩展能拿到的宿主对象 | `DshHost::findObject` | `themeManager` / `translationNotifier` / `connectionManager` / `apiSink` 四个（旧控件壳时代的 `sidebar` / `topbar` / `mainWindow` 随 M8 删除，取到 **null**）。**界面注入点另走一套名字**（`VirtualUiHost::HostItem(...)`，非 `findObject`）：`shell` / `titleBar` / `topBar` / `sessionBar` / `sessionBar.extDock` / `composer`，2026-10-01 起已恢复登记，见 §11.3 的表 |

### 11.2 目标形态：包结构

```
<Name>.ext (zip)
├── regulation.json5        必需
├── main.dll                可选 —— 只有原生逻辑的扩展才需要；纯 QML 扩展可以没有
├── ui/                     扩展自己的 QML（与宿主同名同构：ui/ 下自带组件与子目录）
│   ├── Extension.qml       入口（清单 Entry 未声明时默认取它）
│   └── …
├── icons/*.png             图标；**只允许 PNG**（发布包没有 imageformats，JPEG/SVG 会静默不显示）
└── resources/…             其它资源（宿主不碰，扩展自己用）
```

```json5
// regulation.json5（客户端扩展）
{
  "Name": "plugin-transpiler",    // 安装目录名 = <exe>/clientExtensions/<Name>/
  "Type": "ClientExtension",     // 或 ClientExtensionDebug
  // ---- 扩展身份三件套（2026-10-16 新增；宿主**目前只登记，不消费**）----
  "ID": "plugin-transpiler",     // 标识符，约定**小写 + 连字符**（与 Name 的安装目录名不是一回事）
  "version": "0.1.0",            // 扩展**自身**版本（与 ABIVersion 的界面契约版本不同）
  "dependence": [],              // 依赖的其它扩展 ID 数组；本版不解析依赖顺序、不校验存在性
  "platform": "windows",         // 适用平台（为跨平台做准备）：字符串或数组都认（["windows","linux"]）
  "ABIVersion": 3,                    // 可选：只影响 QML 部分；与宿主不符 ⇒ 该扩展的 ui/ 不落盘，DLL 照旧
  "Entry": "ui/Extension.qml"    // 可选，缺省即此（纯 QML 扩展的入口；DLL 扩展也可以不用它）
}
```

> **`ID` / `version` / `dependence` 三个字段现在只是"登记"**：装载器（`ExtensionLoader::readDescriptor`）会把它们读进
> 描述结构并在启动日志里打一行（`descriptor Name=… Type=… ABIVersion=… ID=… version=… dependence=[…]`），
> **不参与任何判定** —— 不校验依赖是否存在、不按依赖排序装载、不影响 DLL 与 `ui/` 的落盘。
> 三个字段都可省略（省略时日志显示 `(none)` / 空列表）；`Version`（大写 V）不是本约定，请用 `version`。

> `Subtype` 字段（`QQuickSupport` / `QQuickExtension`）与 `.ext` 形式的 Qt Quick 运行时包 **已于 2026-10-01 退役**：
> 宿主自己就是 QML 壳、Qt Quick 随宿主部署，装载器不再读这个字段，也不再按它做分流与预检。

> **没有 `Contributes` / 槽位清单**：扩展自己决定挂在哪个宿主对象下、钉在什么位置（见 §11.3）。

### 11.3 QML 侧架构：**没有槽位，只有"拿到宿主对象直接注入"**

沿用前身项目（旧控件壳）的做法 —— 那时扩展这样挂按钮：

```cpp
QObject* topBarObject = DshHost::findObject(DshHostIndex::kTopBar);          // ① 拿到宿主对象
QHBoxLayout* layout = qobject_cast<VirtualTopBar*>(topBarObject)->GetLayout();
layout->insertWidget(layout->count() - 1, makeButton(topBarObject));         // ② 直接塞进去
QToolButton* button = new QToolButton(qobject_cast<QWidget*>(topBarObject)); // ③ 父对象就是宿主对象
```

QML 里可以**一比一对应**，因为 QML item 本身就是 `QObject`：

| 旧控件壳 | QML 壳 |
|---|---|
| `findObject(kTopBar)` → `QWidget*` | `findObject("topBar")` → `QQuickItem*` |
| `new QToolButton(hostObject)`（父对象 = 宿主对象） | `item->setParentItem(hostItem)` |
| `layout->addWidget(button)` | 宿主那一项若是 `Row`/`Column`（位置器），**注入进去自动排**；不是位置器就由扩展自己的 QML 写 `anchors.*` |

**① 名字契约**（宿主登记、扩展查；复用既有注册表与 C ABI，主线程）：

```cpp
// 宿主：QML 根对象在 Component.onCompleted 里登记（HostUi 是 C++ 支撑的 QML 单例）
HostUi.registerItem("topBar", this)     // TopBar.qml 的根
HostUi.registerItem("sessionBar", this)    // SessionBar.qml 的根
HostUi.registerItem("sessionBar.extDock", extDock)   // 侧栏底部图标行里的"扩展停靠位"（一个 Row）
HostUi.registerItem("shell", this)      // Shell.qml 的根
HostUi.registerItem("titleBar", this); HostUi.registerItem("composer", this); …
// 扩展（DLL）：QObject* obj = DshHost::findObject("topBar");
```

可注入的名字（**稳定契约**，加名字要跟版本一起发）：

| 名字 | 对应文件（根的 Item） | 性质 | 注入后怎么排 |
|---|---|---|---|
| `shell` | `basic/shell/Shell.qml` | 自由 `Item` | 自己写 `anchors` / `x,y`（整屏叠加用） |
| `titleBar` | `skin/window/TitleBar.qml` | 自由 `Item` | 同上 |
| `topBar` | `basic/topbar/TopBar.qml` | `Row`（位置器） | **自动排在末尾**（= `addWidget` 的手感） |
| `sessionBar` | `basic/session/SessionBar.qml` | 自由 `Item` | 自己写 `anchors`（贴底那排就是 `anchors.bottom`） |
| `sessionBar.extDock` | 同上（底部图标行里的一个 `Row`） | `Row`（位置器） | **自动排在末尾** —— 扩展的入口按钮走这里；**按钮与图标都归扩展**，宿主不画没装的扩展的入口。没有扩展注入时它是空的（宽度 0） |
| `panelBarDock.extDock` | `basic/panel/PanelBar.qml`（右栏标题行、窗口按钮左边的 `Row`） | `Row`（位置器） | 同上。**"功能面"类扩展入口走这里**（插件界面 / 面板类），会话级入口才用 `sessionBar.extDock`。2026-10-17 追加 |
| `composer` | `basic/chat/Composer.qml` | 自由 `Item` | 同上 |

**② 注入（DLL 侧）** —— 宿主只给两个原语，怎么挂由扩展自己决定：

```cpp
class VirtualUiHost {                    // 登记为 kUiHost；只许在末尾追加方法（ABI 规则）
    virtual QObject* HostItem(const char* name) = 0;                  // = findObject 的 QML 版
    virtual QStringList HostItemNames() = 0;                          // 自省：现在有哪些可挂
    // 载入扩展的 QML、把 context 注入成它的属性、再 setParentItem 到宿主那一项下面；返回卸载句柄
    virtual QString MountInto(const char* owner, const char* hostItem,
                              const char* qmlUrl, QObject* context) = 0;
    virtual bool UnmountOwner(const char* owner) = 0;                 // 卸载：整批摘掉
};
```
扩展也可以**最原子地自己来**（完全照旧模型）：`HostItem("topBar")` → 自己 `QQmlComponent` 建 item →
`setParentItem(topBar)`。

**③ 注入（纯 QML 扩展，没有 DLL）** —— 同一个名字契约，走 QML 单例，位置写在扩展自己的 QML 里：

```qml
// 扩展的 ui/TopBarButton.qml
IconButton {
    parent: HostUi.item("topBar")            // 注入点
    // 什么都不写也行：父项是 Row，位置器会排它
    glyph: "★"
    onClicked: HostUi.mountInto("plugin-transpiler", "shell", "ui/Panel.qml", null)
}
```

**宿主侧只有两件事**：(a) 把可挂对象按稳定名字登记出来；(b) 记账 `owner → 注入项`，卸载时整批删。
没有槽位表、没有贡献清单、没有 mount 时机编排。

### 11.4 两个系统的互通契约（DLL ↔ 它自己的 QML）

这是"一个扩展同时带 DLL 与 QML"的关键：宿主把 **DLL 的 QObject 注入成扩展 QML 根的属性**，之后两边
用**原生元对象调用**互通（参数完整、有类型、不经字符串转发）。

接口定义见 §11.3（`HostItem` / `HostItemNames` / `MountInto` / `UnmountOwner`）。这里只讲**互通**：

```qml
// 扩展自己的 ui/Panel.qml
Item {
    required property var context              // = DLL 传进来的那个 QObject

    Connections {                              // ① DLL → QML：DLL 发信号，QML 响应
        target: context
        function onStateChanged() { label.text = context.stateText }
    }

    MouseArea { onClicked: context.requestRefresh() }   // ② QML → DLL：调 DLL 的 Q_INVOKABLE
                                                       //    也可以 emit 一个 QML 信号，DLL 用槽接
}
```

四条硬约束：

1. **线程**：QML/JS 只在 GUI 线程跑。DLL 若在工作线程，必须 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 排队过来。
2. **生命周期**：`context` 必须活得比视图久；扩展卸载/退出时宿主先 `Unmount`、再让插件 `detachHost()`（顺序反了就是访问已卸载的 DLL）。
3. **失败隔离**：每个视图一个 `Loader`，一个扩展报错只摘它自己那一项。
4. **信任模型**：扩展 QML 与宿主**同一个 QML 引擎** ⇒ 它能访问所有宿主单例（`Hub` / `Theme` / `HostConnections`）——
   所以扩展 QML 是**可信代码**，不是沙箱。真要隔离只能另起进程/引擎（现在没有）。

### 11.5 ABI 与版本

| 契约 | 载体 | 现在 | 不一致时 |
|---|---|---|---|
| C++ 插件面 | `VirtualClass/VirtualCommon.h` 的虚接口 | `VirtualConnectionManager` / `VirtualTheme` / `VirtualTranslation` / `VirtualApiSink` / `VirtualUiHost` / `VirtualBlockBackendHost` | 方法只许**末尾追加**；DLL 与宿主版本不匹配时 `qobject_cast` 失败即视为不实现 |
| 宿主界面契约 | `pack.json` 的 `uiAbi`（皮肤包）/ 客户端扩展 `regulation.json5` 的 `ABIVersion` | **3**（`kStylePackUiAbi`） | 皮肤包 ⇒ 整包拒载回退内置；扩展 ⇒ **只装 DLL，不落该扩展的 `ui/`**（DLL 部分照旧） |

### 11.6 最小示例（`..\plugin-transpiler` 就是活样例）

```
..\plugin-transpiler/
├── regulation.json5      Name=plugin-transpiler, Type=ClientExtension, ABIVersion=3
├── main.cpp              attachHost()：
│                           auto* ui = qobject_cast<VirtualUiHost*>(DshHost::findObject(DshHostIndex::kUiHost));
│                           ui->MountInto("plugin-transpiler", "shell", "<自己的>/ui/PluginTranspiler.qml", this);
├── ui/EntryButton.qml     入口按钮（自绘，注入 sessionBar.extDock；required property var context）
├── ui/PluginTranspiler.qml  required property var context（required 属性由宿主 createWithInitialProperties 注入）；
│                           面板只读宿主 Theme token（`import ULinHub 1.0`），不写死配色
└── PluginTranspiler.vcxproj  MSBuild 构建 main.dll（POST_BUILD 把 regulation.json5 + ui/ 拷到部署目录）
```

> 样例扩展 `extensions/QuickOrbit/` 于 2026-10-10 删除：它走的是"自己 `new QQuickView` 开独立窗口"的
> 旧路，依赖的 `QQuickSupport` 运行时包与 `Subtype` 门控都已退役，且从没部署过 —— 要看真样例就看
> `..\plugin-transpiler`（侧栏入口：扩展自绘按钮注入 `sessionBar.extDock`）与 `..\codex-backend`（后端接管 + 注入面板）。

**侧栏入口怎么接**：**按钮归扩展自己** —— 扩展自绘一颗入口按钮，注入宿主的侧栏停靠位
`sessionBar.extDock`（`Row`，位置器自动排布；所以扩展 QML **不写 x / y / anchors**，只给宽高）：

```qml
// <扩展>/ui/EntryButton.qml —— 入口连图标一起归扩展，宿主不画
Item {
    required property var context          // = DLL 传进来的桥，见 §11.4
    width: 32; height: 32
    Text { anchors.centerIn: parent; text: "⇄" }
    MouseArea { anchors.fill: parent; onClicked: context.togglePanel() }
}
```

```cpp
// attachHost() 里（⚠️ 用 0ms 定时器延后，理由见 §11.3 的登记时机）
ui->MountInto("plugin-transpiler", "sessionBar.extDock", "<自己的>/ui/EntryButton.qml", bridge);
```

> **宿主不摆"未装扩展"的哑按钮**：插件转译器不是宿主的功能，停靠位在没有扩展注入时是空的。
> 旧做法（宿主 `SessionBar.qml` 画一颗按钮 + 登记 `qml.sessionBar.openPluginTranspiler`，扩展用
> `TakeoverConnection` 接管、`ReleaseTakeover` 交还）**已于 2026-10-12 退役** —— 它会让宿主在
> 扩展没装时露出一颗点了没反应的按钮。要接管宿主**既有**按钮 / 信号，机制本身仍在
> （`VirtualConnectionManager`），`..\codex-backend` 是活样例。
> 副作用提醒：给扩展按钮**另起 owner**（PanelAnimDemo 用 `<扩展名>/panel` 给面板）——
> `UnmountOwner` 是按 owner 整批删的，入口按钮与面板共用一个 owner 会把按钮一起摘掉。

### 11.7 顶替面板坞的块后端（`VirtualBlockBackendHost`，2026-10-15 加的接缝）

§11.3 的注入是"往宿主对象下面**追加**"。若要的是**替换** —— 比如让终端块不再走内置的
服务端引擎，改用扩展自己的（本地）终端 —— 走另一条接口 `VirtualBlockBackendHost`
（`kUiHost` → `HostUi` 单例，与 §11.3 的注入共用同一个宿主面）。

**它换的是"后端"，不是"界面"**：终端块照旧由宿主的 `TerminalPanel` + WebEngine + xterm.js 渲染
（底色跟主题走），扩展只提供页面背后那个对象 —— **扩展不用写任何 QML**。宿主把该对象原样注册到
这个块的 `QWebChannel` 上（id `ulinterm`），页面直连它。

```cpp
// attachHost() 里直接调即可 —— 它只往 HostUi 写一条登记、不碰宿主 QML 对象，没有 §11.3 的
// "等 registerItem" 时序问题。⚠️ 但必须赶在该块的面板被创建之前：面板只在创建时取一次后端。
auto* panels = qobject_cast<VirtualBlockBackendHost*>(DshHost::findObject(DshHostIndex::kUiHost));
if (panels) {
    panels->SetBlockBackend(extensionDirName(),   // owner：撤的时候认它
                            "terminal",            // kind：当前只有 "terminal" 落地
                            m_backend);            // 你的后端对象（契约见下表）
}
// detachHost() 里撤（也可以不写 —— 宿主卸载扩展时会按 owner 整批撤，见第 3 条）
panels->ClearBlockBackend(extensionDirName(), "terminal");
```

**后端对象的契约** —— 就是宿主页面 `qrc:/ULinHub/Terminal/terminal.html` 认的那套，与宿主
`TerminalBridge` 的"页面面"同构。每个调用都带 `blockId`：同一个对象可能被多个终端块的面板注册，
页面按自己的块过滤上行信号。

| 方向 | 成员 | 说明 |
|---|---|---|
| 页面 → 后端 | `pageHello()` | 页面刚接上信道；可空实现 |
| | `start(blockId, sessionId)` | 这个块开始用终端了（`sessionId` 由宿主给，只是参考信息） |
| | `pageReady(blockId)` | 页面已建好 xterm，可以喂帧了 |
| | `pageGone(blockId)` | 页面要销毁/换绑（切块、收右栏）—— **别在这里关终端** |
| | `write(blockId, data)` | 键盘 / 粘贴的原始串 |
| | `resize(blockId, cols, rows)` | addon-fit 量出来的网格尺寸 |
| | `ack(blockId)` | 上一帧已写进 xterm（背压放行） |
| | `log(blockId, message)` | 页面里的诊断串（页面 console 不落盘） |
| | `closeBlock(blockId)` | 关块（页签 ✕）—— 真该收尾的地方 |
| 后端 → 页面 | `frameReady(blockId, kind, sequence, text)` | `kind = "snapshot"`（页面先 reset 再写）或 `"output"` |
| | `statusChanged(blockId, state, statusText, readOnly, title)` | 状态行；`state == "attached"` 时页面清掉提示层 |

槽用 `Q_INVOKABLE` 就够（不必是 `signals`），但两个上行信号**必须**是信号（页面 `connect` 它们）。
对象要自带 `objectName = "ulinterm"`：宿主按它的 `objectName` 注册；空着宿主会替它设。

三条容易踩的**页面侧**约定（后端实现者必读）：

- **一个对象服务多个块**：信道对象是按面板注册的，但同一个后端对象会被多个终端块的面板用到 ——
  这就是每个调用都带 `blockId` 的原因。**按块存状态**（会话表），别用单例字段。
- **`ack` 是背压，不是可选项**：页面 `term.write` 是异步的，写完才回 `ack`。后端应当"发一帧、
  等 ack、再发下一帧"；如果只顾自己推，`dir /s` 这类能一次性产生几十兆输出。若不想实现背压，
  至少要**有界**地丢/排队，别无限攒。
- **推给页面的 `text` 必须是合法 UTF-8 字符串**（QWebChannel 传的是字符串，不是字节）。
  自己产生字节流的后端（如本地 PTY）要注意：**字节流会在任意位置被切开**，跨帧的多字节序列
  必须自己续上，否则汉字会变成 `U+FFFD`。

顶替后的行为：

| 项 | 顶替后 |
|---|---|
| 启动器里那条"终端" | **保留**（还是"终端"，点了照常开一个终端块） |
| 块的内容 | 宿主的 `TerminalPanel`（**不变**），只是信道上的对象换成了扩展的后端 |
| 界面 / 配色 | 宿主的（暗色纯黑、亮色 3% 灰，跟 `Theme.isDark` 走）—— 扩展不参与 |
| 对 WebEngine 的依赖 | **仍在**（页面是宿主渲染的）。想彻底脱开 WebEngine 就得自带界面，那走 §11.3 注入 + 自己画 |
| 关块（页签 ✕） | 宿主照常调 `Terminal.closeBlock`，桥按后端转给你的 `closeBlock(blockId)` |

四条约定：

1. **按 kind 只记一份后端。** 后登记者取得该 kind，前一份被顶掉；所以 `ClearBlockBackend`
   认 owner —— 旧扩展卸载时不会把新扩展顶上去的登记清掉。
2. **登记时机是启动期**（`attachHost()`）：面板只在创建时读一次后端，晚了它已经接上内置桥。
3. **卸载不用自己兜底**：宿主在 `ClientExtension::remove` 那条路上按 owner 整批撤
   （`HostUi::releaseBlockBackends`，与 `ReleaseTakeoversFor` 并列）。
4. ⚠️ 顶替的是**块后端**，不动启动器条目与块标题。想让用户完全看不到宿主的终端入口，那是改
   界面副本 `<exe>/ui/basic/shell/PanelBarDockView.qml` 的事，与扩展无关。

**接缝插在哪儿（宿主侧的分层）**：`kind → 视图` 的映射表在 C++
（`PanelBarDockModel::viewFor`：逻辑路径 + 要不要当前块 + 兜底文案，与中文块名 `kindLabel()` 同层），
QML（`PanelBarDockView.qml`）只按那组值装机 —— 路由表达式里没有任何 kind 字符串，**也不看后端登记**
（后端是"页面接谁"的事，与"装哪个视图"无关）。宿主侧只有两处读这份登记：`TerminalPanel.qml`
创建时取一次、交给 `Terminal.registerOn(channel, backend)`，以及 `TerminalBridge` 把
`pageGone` / `closeBlock` 转给后端。

**活样例**：`<ULin>\local-terminal`（VS 工程，宿主仓库的兄弟目录）—— 最小可跑的后端顶替：
`src/LocalTerminalPlugin.cpp` 登记、`src/LocalTerminalBackend.h/.cpp` 是后端对象本体
（每块一个会话）、`src/ConPtyProcess.h/.cpp` 用 `CreatePseudoConsole` 起本地终端。

**为什么是加接缝而不是让扩展硬顶**：面板坞的块由 C++ 模型决定（`launcherItems` 是 CONSTANT），
"页面接谁"由宿主 QML 在 `Component.onCompleted` 里注册信道时定，且终端这条链路没进
`ConnectionManager` 的登记表 —— "接管信号 / 抢 QWebChannel 注册"都够不着它。
`VirtualBlockBackendHost` 把"块后端由谁提供"变成一个**显式的可登记点**：宿主改一处，扩展不必猜对象、
不必抢时机。

### 11.8 在右栏开一块"带地址的块"（2026-10-17）

用途：插件自己 serve 一个页面，把它开成右栏的一块块。那块与"网页浏览"是**同一块**
（kind = `browser`、宿主的 `WebBrowserPanel`），所以外观/行为不需要任何新东西 ——
需要只是"谁能请块模型追加一块带初始地址的块"。

**分两层：底下原函数保持自然，上面套一层虚接口做加法。**

| 层 | 内容 |
|---|---|
| 原函数（自然能力，与扩展无关） | `PanelBarDockModel::open(kind)`（原有）、`openUrl(url, title)` 返回块 id、`closeBlock(id)`、`blockIds()`。这些是"块模型本来就该有的事"（开一块带初始地址的浏览器块、按稳定 id 关、列出 id） |
| 虚接口层（加法·纯转发） | `VirtualBlockBackendHost::OpenBlockUrl(url, title)` / `CloseBlock(blockId)` —— `HostUi` 的实现只调用上面两个原函数；`VirtualMain::ServerBaseUrl()` 的实现只 `return baseUrl();` |

三条**合法**的提供方式，本版**前两条都已落地、第三条天然可用**，可并存：

1. **把对象直接提供出去**：块模型本体登记为 `kPanelBarDock`（[HostIndex.h] 有完整说明）。扩展取到后
   能调它**全部** `Q_INVOKABLE`（`open` / `openUrl` / `closeBlock` / `blockIds` / `close` / `activate` /
   `showHome`），也能直接 `connect` 信号；
2. **给函数套虚接口层**：`VirtualBlockBackendHost::OpenBlockUrl` / `CloseBlock`、`VirtualMain::ServerBaseUrl`
   —— 实现只转发/套壳；
3. **作为槽**：上面那些方法本来就是 `Q_INVOKABLE`，信号可 `connect`（第 1 条与第 3 条是同一批东西的两种用法）。

非法的是：前三者能完成工作时，再刻意加一批"一看就是给扩展用的"函数。
本版所有接口都属三种之内，且原函数没有为扩展改动过一行。

**选哪条**：只要"开一块 / 关一块"，用第 2 条（虚接口）最小耦合；要看更多状态（活动块、块列表、
信号）或想省掉转发层，直取 `kPanelBarDock` 的模型本体。

```cpp
// attachHost() 里（只碰 C++ 侧对象，没有"等 QML 登记"的时序问题）
auto* panels = qobject_cast<VirtualBlockBackendHost*>(DshHost::findObject(DshHostIndex::kUiHost));
if (!panels) { /* 宿主太旧：没有这个接口 */ return; }

// 引擎地址（拼同源页面 URL 用；接管态下为空）
QUrl base;
if (auto* api = qobject_cast<VirtualMain*>(DshHost::findObject(DshHostIndex::kMainWindow)))
    base = api->ServerBaseUrl();

// 开一块：与用户点"网页浏览"同一条路径（同类编号 / 追加即激活 / 离开主界面）
const QString blockId = panels->OpenBlockUrl(base.toString() + QStringLiteral("/plugin-ui/myplugin/"),
                                             QStringLiteral("插件 A"));
// blockId 由**调用方**记住（模型不记"是谁开的"）；关的时候原样传回：
panels->CloseBlock(blockId);
```

| 成员 | 语义 |
|---|---|
| `OpenBlockUrl(url, title)` | 追加一块 browser 块并激活，返回 **块 id**（`"blk-N"`）；`url` 为空 ⇒ 返回空串，什么都不开；`title` 空 ⇒ 用内置块名"网页浏览 N" |
| `CloseBlock(blockId)` | 按稳定 id 关（与按索引关同一条收敛逻辑）；id 不存在/已关 ⇒ `false` |
| `ServerBaseUrl()` | 干净的引擎 `baseUrl`（无 query/fragment、不含启动令牌；接管态为空） |

**入口就是启动器里的第四行"插件界面"（2026-10-17）**

面板坞的启动器（没有活动块时那一页）现在有四行：终端 / 网页浏览 / 文件浏览 / **插件界面**。
第四行点开是一块 `pluginUi` 块，**视图仍是宿主的浏览器面板**（地址栏/前进后退/主题全一样），
初始地址取自块模型的 `pluginPageUrl` 数据属性 —— 扩展在装载期写一次：

```cpp
QObject* dock = DshHost::findObject(DshHostIndex::kPanelBarDock).data();   // 对象直给
dock->setProperty("pluginPageUrl", QStringLiteral("http://127.0.0.1:8765/"));  // 纯数据，无函数
```

要点：

1. **扩展不画入口**：入口是宿主启动器的一行（与另外三个块同形），扩展只提供"显示哪个页面"；
2. `pluginPageUrl` 是 `Q_PROPERTY(... WRITE ...)`，写它是 `setProperty`，不是调新函数；
3. 用户自己开的"网页浏览"块**不受** `pluginPageUrl` 影响（`activeViewUrl` 只在 `pluginUi` 块上回落它）；
4. 若某个构建还需要"放进侧栏/标题行的按钮"那种入口，`panelBarDock.extDock` / `sessionBar.extDock`
   两个停靠位仍在（`HostUi.registerItem` 登记名，注入方式见 §11.3）；挂载要用
   `HostUi::namesChanged` 信号等停靠位出现，**别用 0ms 定时器**（装载期可能不触发，实测）。
**插件设置落点（`pluginUi.settings`，2026-10-17）**

生态里 13 个带界面的第三方插件，**10 个把界面挂在设置页**（`settings.section` / `settings.*`）。
壳的"插件界面"面板（`basic/panelui/PluginUiPanel.qml`）里留了一个**设置区**，登记名
`pluginUi.settings`（一个 `Column`）；扩展可以按 `adapters.json` 把"原来在设置页里的东西"
搬到这个区里，一块插件一张卡片。

```json
// <扩展目录>/adapters.json
{ "sections": [
  { "plugin": "dsh-plugin-appshot", "slot": "settings.section", "title": "截图设置（appshot）",
    "detail": "原位置：DSH 设置页分区。自带 JSON 数据面。", "actionLabel": "查看设置数据",
    "api": "/plugins/appshot/config" }
] }
```

扩展侧做三件事：读 `adapters.json` → 连 `HostUi::namesChanged` 等 `pluginUi.settings` 出现 →
`MountInto(owner, "pluginUi.settings", "<扩展>/ui/PluginSettingSection.qml", bridge)`。
`api` 非空时卡片带一个按钮，把它开成一块 browser 块看数据（下一步换成原生表单）。

**取数是原生的（实测 2026-10-17）**

卡片不再"开一块网页看 JSON"：`SectionBridge` 用 `QNetworkAccessManager` 直接 GET 插件的数据面，
把 JSON 展平成 `rows`（key/value）交给 QML 原生渲染 —— 全程不碰 WebEngine。

两个实测要点：

1. **引擎地址要现取**：扩展装载时引擎往往还没打印 `dsh web:` 地址，所以 `refresh()` 里每次现取
   `VirtualMain::ServerBaseUrl()`；取不到就自动重试（1.5s × 8）。
2. **认证栅栏只挡住 SPA 根**：`GET /` 返回 **401**（要 `dsh-auth` cookie，扩展没有）；
   而插件自己 `webServer.register` 的路由**不带 cookie 也能取**（前面 §11.8 已实测）。
   ⇒ 取插件数据不需要令牌，取引擎 SPA 资源才需要。
3. 数据面没装插件时是 **404**（`/dsh-time-machine/snapshots`、`/plugins/appshot/config` 实测）；
   要看到真实行，得先把那个插件装进 profile。

**公共落点表（2026-10-18）**

原则：**落点是公共的，代码里没有插件特例**；一个插件界面"原来在哪、用什么渲染"由适配器声明。
宿主提供三种能力，扩展按插件实际提供的东西选：

| 插件提供的东西 | 落点 | 宿主能力 |
|---|---|---|
| JSON 数据面（`/plugins/x/...`） | 原生（键值 + 可编辑 + 台账） | `OpenBlockUrl` / 门户面板（QML） |
| **自 serve 的 HTML 页面** | **网页窗口**（WebEngine 解析，前进/后退/刷新） | `OpenWebWindow(owner, url, title)`（2026-10-18 追加，通用） |
| 只有 React 客户端半（无页面、无数据面） | 无解（如实标注） | — |

适配器字段：`"web": "" | "engine" | "http(s)://…" | "/相对路径"`。

实测结论（2026-10-18）：

1. **插件自 serve 的页面**：可用。验证页 `http://127.0.0.1:8765/` 在 WebEngine 窗口里完整渲染。
2. **`"web": "engine"`（引擎自带 SPA）**：**不可用** —— 窗口能开、页面能载，但 SPA 报
   `web boot: window.__ModuleLoader__ bootstrap facade is missing`（`dsh web` 的启动壳只有 web 部署才有）；
   所以别把 `engine` 当作万能兜底。
3. `dsh-plugin-appshot` 属于第三种：默认热键是"左右 Ctrl 同时按"（无右 Ctrl 的键盘用不了，可改
   `windowsHotkeys` / `shortcutMode`），截图结果要靠它的 React 客户端消费 `/pending` —— 我们既没有可用
   页面、又无原生图片显示，所以本版只到"设置可读写"。

**字段名怎么翻译（2026-10-18）**

插件的数据面只给 `key`（`shortcutMode`）和 `value`（`double-ctrl`），**没有文案**。三层来源按优先级取：

| 优先级 | 来源 | 说明 |
|---|---|---|
| 1 | `adapters.json` 的 `labels` / `values` | 我们自己写的词条，最准（`"soundEnabled": "提示音"`、`"shortcutMode#double-ctrl": "双击 Ctrl"`） |
| 2 | **插件自带词条表** | 扫插件包 `dist/client.js`（及 `client.js` 等同位文件）里 `<标识符>: "<含中文的串>"` 的字面量表。实测：`dsh-electro-lab` 带（`languageAuto: "自动（跟随文档）"`），`dsh-plugin-appshot` 不带（它自己的界面就是英文硬编码） |
| 3 | 内置词典 | `platform→平台`、`shortcutMode→快捷键方式`、`soundEnabled→提示音`、`animationEnabled→动画效果`、`windowsHotkeys→Windows 快捷键`…，值是布尔就显示 开/关，快捷键表显示成 `左 Ctrl / 右 Ctrl` |

三个落地细节：

1. 抽词条是**启发式**（正则扫字面量），所以 `adapters.json` 的词条永远优先，用来纠偏；
2. 写回用的是**原始 key**、编辑框里是**原始 value**，只有显示层被翻译（不要把 `开` 发回服务端）；
3. 有些插件用宿主的 locale 服务（`t$1("languageAuto")`，只有 key，文案在宿主侧）——这类我们拿不到表，会落到内置词典/原始 key。

**设置项可写 + 落了没人接就隐藏（2026-10-18）**

两件事都靠 `adapters.json` 的两个新字段：

```json
{ "plugin": "dsh-plugin-appshot", "slot": "settings.section", "title": "截图设置（appshot）",
  "api": "/plugins/appshot/config",
  "installDir": "dsh-plugin-appshot",   // 检测"装没装"：profile/node_modules/<这个>
  "writable": true }                    // 允许在仪表盘里改（POST 合并补丁）
```

1. **隐藏**：`installDir` 对不上（没装）或数据面回 **404** ⇒ 该分区不出现在侧栏/仪表盘里；
   整块都没有分区时侧栏的"插件设置"标题也一起隐藏（宿主面板按容器子项数决定）。
2. **写回**：仪表盘卡片上的「编辑」→ 每行变成输入框 + 保存 → `POST <api>`，
   body 是 `{"<key>": <value>}`（布尔/数字/JSON 会还原成真类型）。**写的是插件自己的接口**，
   不拦截、不改插件代码；成功后立刻 GET 读回真实值。
3. 引擎地址在装载期还没发布 ⇒ 读与写都会**排队重试**（1.5s × 8），用户的编辑不会丢。

**插件数据块（`pluginData`）：原生选项卡，不是网页**

点卡片上的「在新选项卡打开」会开一块 **`pluginData`** 块。它的外壳是宿主的
`basic/panelui/PluginDataPanel.qml`（标题 + 空态 + 一个名为 **`pluginData.host`** 的容器），
**内容由扩展注入**：

```cpp
// 1) 开块（模型自己的 Q_INVOKABLE，与"网页浏览"同一个 open(kind)）
QMetaObject::invokeMethod(dock, "open", Q_ARG(QString, QStringLiteral("pluginData")));
// 2) 等 pluginData.host 被登记（namesChanged）后注入原生视图
ui->MountInto(owner, "pluginData.host", "<扩展>/ui/PluginDataView.qml", bridge);
```

视图里的数据由 `SectionBridge` 用 `QNetworkAccessManager` 取（**不经过 WebEngine**），
展平成 rows 原生渲染；`刷新` 由用户触发，也支持自动重试等引擎地址就绪。

**DSH slot → 我们壳里的落点（当前覆盖）**

| DSH slot | 我们的落点 | 状态 |
|---|---|---|
| `settings.section` / `settings.*` / `plugins.*` | 插件界面面板的 `pluginUi.settings` 区 | ✅ 已通（`adapters.json`） |
| `sidebar.right.pane.tab` | 右栏面板坞的块（`openUrl`） | ✅ 已通 |
| `sidebar.footer.action` | 左栏 `sessionBar.extDock`（Row） | ✅ 已通（注入） |
| `shell.overlay` / `conversation.composer` | `shell` / `composer` 注入点 | ✅ 已通（注入） |
| `conversation.chat.node` / `tool.call.toolview` / 会话头 | — | ❌ 未映射（如实标注） |

**插件侧要做什么**

插件的 GUI 必须能以**独立页面**打开 —— 用 `ctx.webServer.register` 注册路由返回 HTML：

```js
export function apply(ctx) {
  ctx.inject(['webServer'], (webCtx) => {
    webCtx.effect(() => webCtx.webServer.register({
      kind: 'exact',
      path: '/plugin-ui/myplugin/',
      handler: async (req, res) => {
        res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
        res.end('<!doctype html><div id="root"></div><script src="/plugin-ui/myplugin/app.js"></script>');
      },
    }));
  });
}
```

三条**实测过**的约定（2026-10-17，引擎副本）：HTML/JS/CSS 都能发；与 `/api/*` 同源（页面可
直接 `fetch`）；⚠️ 自定义路由**不在认证栅栏后面**（不带 cookie 也 200，鉴权得插件自己负责）。

四条容易踩的：

1. **URL 必须完整**（含 scheme）：宿主视图走"程序化打开"入口，故意不过 `UrlInput` 的输入规整；
2. **谁开的谁记 id**：模型不记归属；块被用户点 ✕ 关掉后宿主不会通知你，要判断就 `blockIds()`；
3. `openUrl` 与 `open` 同一条通知节奏（多一次 `activeViewChanged` 就多播一次内容重放动画）；
4. 卸载扩展时自己 `CloseBlock`（宿主不按归属清块）。

**生态现状（重要）**：第三方插件的 GUI 基本都是 `dsh.client` 的 React 客户端半（跑在 DSH web
客户端的 slot 里，例如 `dsh-client-ui-scan2ebook` 的右栏 tab），**Qt 壳渲染不了**。能进这块的只有：
①插件自 serve 的页面；②由扩展/宿主按插件数据重画的界面（②需要"页面 + 数据桥"，下一步）。