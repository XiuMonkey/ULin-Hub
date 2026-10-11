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

- 管道名：`\\.\pipe\dshhub-bridge`；
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
> `HostUi` / `VirtualUiHost`）**已于 2026-10-01 实现**（§11.3 就是实现后的形态）。11.1 表里"扩展能拿到的宿主对象"那句仍是旧的：现在可挂对象是 `shell` / `titleBar` / `topBar` / `sidebar` / `composer`。

### 11.1 现状（已实现，可直接用）

| 能力 | 实现 | 说明 |
|---|---|---|
| 安装 | `ExtensionLoader::loadAndInstall` | 解压到临时目录 → 按 `regulation.json5` 的 `Type` 分流 → 客户端扩展**整包落地**到 `<exe>/clientExtensions/<Name>/`（dll、regulation 之外的 **QML 与资源也一并拷过去**） |
| DLL 装载 | `ClientExtension::loadAll()` / `loadOne()` | 启动时（`engine.load()` 之后）扫目录：没有 `regulation.json5` 的跳过；`main.dll` 优先，否则取目录里第一个 `*.dll`；`QPluginLoader` → `qobject_cast<DshHostPlugin*>` → `setHostIdentity(目录名)` → `attachHost()`。全程 **GUI 线程** |
| 卸载 | `ClientExtension::remove()` | 先删判据（`regulation.json5`）⇒ 不再算已安装；dll 被占用则留残留，下次启动 `sweepPendingRemovals()` 清 |
| 管理界面 | `Hub.extensions`（`ExtensionManagerController` + `basic/extensions/ExtensionManagerPage.qml`） | 两种来源：工具扩展（`profiles/web/extensions.json`）、客户端扩展（`<exe>/clientExtensions`） |
| 扩展能拿到的宿主对象 | `DshHost::findObject` | `themeManager` / `translationNotifier` / `connectionManager` / `apiSink` 四个。旧控件壳时代的 `sidebar` / `topbar` / `mainWindow` 已于 M8 删除，取到 **null** |

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
  "Name": "PluginTranspiler",    // 安装目录名 = <exe>/clientExtensions/<Name>/
  "Type": "ClientExtension",     // 或 ClientExtensionDebug
  "Version": "0.1.0",            // 可选
  "ABIVersion": 3,                    // 可选：只影响 QML 部分；与宿主不符 ⇒ 该扩展的 ui/ 不落盘，DLL 照旧
  "Entry": "ui/Extension.qml"    // 可选，缺省即此（纯 QML 扩展的入口；DLL 扩展也可以不用它）
}
```

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
HostUi.registerItem("sidebar", this)    // Sidebar.qml 的根
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
| `sidebar` | `basic/sidebar/Sidebar.qml` | 自由 `Item` | 自己写 `anchors`（贴底那排就是 `anchors.bottom`） |
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
    onClicked: HostUi.mountInto("PluginTranspiler", "shell", "ui/Panel.qml", null)
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
| C++ 插件面 | `VirtualClass/VirtualCommon.h` 的虚接口 | `VirtualConnectionManager` / `VirtualTheme` / `VirtualTranslation` / `VirtualApiSink`（+ 目标形态的 `VirtualUiHost`） | 方法只许**末尾追加**；DLL 与宿主版本不匹配时 `qobject_cast` 失败即视为不实现 |
| 宿主界面契约 | `pack.json` 的 `uiAbi`（皮肤包）/ 客户端扩展 `regulation.json5` 的 `ABIVersion` | **3**（`kStylePackUiAbi`） | 皮肤包 ⇒ 整包拒载回退内置；扩展 ⇒ **只装 DLL，不落该扩展的 `ui/`**（DLL 部分照旧） |

### 11.6 最小示例（`extensions/PluginTranspiler` 就是活样例）

```
extensions/PluginTranspiler/
├── regulation.json5      Name=PluginTranspiler, Type=ClientExtension, ABIVersion=3
├── main.cpp              attachHost()：
│                           auto* ui = qobject_cast<VirtualUiHost*>(DshHost::findObject(DshHostIndex::kUiHost));
│                           ui->MountInto("PluginTranspiler", "shell", "<自己的>/ui/PluginTranspiler.qml", this);
├── ui/PluginTranspiler.qml  required property var context（required 属性由宿主 createWithInitialProperties 注入）；
│                           面板只读宿主 Theme token（`import ULinHub 1.0`），不写死配色
└── PluginTranspiler.vcxproj  MSBuild 构建 main.dll（POST_BUILD 把 regulation.json5 + ui/ 拷到部署目录）
```

> 样例扩展 `extensions/QuickOrbit/` 于 2026-10-10 删除：它走的是"自己 `new QQuickView` 开独立窗口"的
> 旧路，依赖的 `QQuickSupport` 运行时包与 `Subtype` 门控都已退役，且从没部署过 —— 要看真样例就看
> `extensions/PluginTranspiler`（侧栏入口接管）与 `extensions/CodexBackend`（后端接管 + 注入面板）。

**侧栏入口怎么接（比挂面板更常用）**：宿主 QML 用一句话登记一个入口，

```qml
// source/qml/basic/sidebar/Sidebar.qml 的 Component.onCompleted
HostConnections.RegisterConnection("qml.sidebar.openPluginTranspiler", sidebar,
                                   "openTranspilerRequested()", sidebar, "handleOpenTranspiler()")
```

DLL 扩展在 `attachHost()` 里**接管**它（拿 `kConnectionManager` → `VirtualConnectionManager`）：

```cpp
manager->TakeoverConnection(QStringLiteral("qml.sidebar.openPluginTranspiler"),
                            this, QByteArrayLiteral("1onTranspilerEntryClicked()"));  // '1' = 槽
```

接管成功后那颗按钮的信号直接进扩展的槽；退场时 `ReleaseTakeover(index)` 交还。
失败模式：**接管没有回执** —— index 拼错 / 宿主没登记时，宿主只写一行 warn，扩展侧看不出来。
