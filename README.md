# ULin Hub

ULin Hub is the continuation product based on the DSH Hub prototype. This baseline carries over the prototype's current working-tree source, resources, local server runtime, extension examples, tooling, and tests. The prototype remains separate and unchanged.

The first baseline intentionally keeps the existing Qt Widgets UI and build behavior. QML/QQmlApplicationEngine migration will be done as a separate UI-only phase.

## Project layout

- `source/` — Qt/C++ application and CMake/Visual Studio projects
- `assets/` — application resources, translations, themes, and bundled server files
- `assets/resources/server/` — Node runtime, server dependencies, harness configuration, and launch-root data
- `extensions/` — client extension sources and examples (CodexBackend, PluginTranspiler)
- `misc/legacy-tests/` — C++ unit tests
- `misc/tools/` — build, packaging, and diagnostic scripts

## Build

The CMake entry point is `source/CMake/CMakeLists.txt`. Configure from the repository root with a Qt 6.5+ installation. On Windows, `source/ULin Hub.slnx` is the Visual Studio solution.

## 命名词表（2026-10-18 统一，新增代码照此写）

一条特性只有一个名字，**从 QML 类型、C++ 类、token、扩展登记名、注入点、连接 id、翻译 id 一路到尾**。
下表右侧一列是已废弃的旧写法，grep 到就是漏改（历史文档里引用**已删除的旧控件壳**时除外）。

| 概念 | 唯一名 | 派生名 | 已废弃 |
|---|---|---|---|
| 左栏 | `SessionBar` | `sessionBarWidth`（尺寸）、`sessionBarBg`（颜色）、`sessionBar_*`（翻译 id） | `sidebar`、`Sidebar.qml`、`sidebarBg`、`sidebar_*` |
| 右栏（整栏卡片） | `PanelBar` | `panelBarWidth`、`panelBarMinWidth`、`panelBarGap` 语境下的 `panelGap` | `right sidebar`、`rightInset`、`rightColumn` |
| 右栏的坞（页签 + 启动器 + 内容） | `PanelBarDock` | 模型 `PanelBarDockModel`、视图 `PanelBarDockView.qml`、QML 单例 `PanelBarDock` | `PanelDock`、`Dock`、`panelDock` |
| 坞里的"块" | `Block`（代码）/「页签」（UI 文案） | 种类叫 `kind`：`terminal` / `browser` / `files` / `pluginUi` / `pluginData` | `tab`（指块时） |
| 扩展登记名 | `sessionBar`、`panelBarDock` | `kSessionBar`、`kPanelBarDock` | `sidebar`、`panelDock`、`kSidebar`、`kPanelDock` |
| 扩展注入点 | `sessionBar.extDock`、`panelBarDock.extDock` | — | `sidebar.extDock`、`panelBar.extDock` |
| 连接 id | `qml.sessionBar.*` | — | `qml.sidebar.*` |
| 翻译 id | `sessionBar_*`、`panelBar_*`、`topbar_panelBar_tip` | — | `sidebar_*`、`right_sidebar_*` |
| 后端（客户端拉起的 DSH 进程） | 用户可见文案一律「服务端」 | 代码标识符保留 `Server*` / `DshApiClient` | 「服务器」、「引擎」（指 DSH 时）、「后端」 |

⚠️ 三条硬约束：

1. **改名必须一次改齐 host + 所有扩展**。登记名是纯字符串、没有编译期检查，host 改了扩展没改 =
   扩展**静默失效**（不报错、功能没了）。当前全部扩展源码在 `..\` 下（Plugin Transpiler /
   DSH Plugin UI Supporter / Codex Backend / Local Terminal），改名时一起改、一起重编、一起部署。
2. **ABI 不改名**：`VirtualClass/VirtualCommon.h` 的虚方法只许末尾追加（不改名 / 不换签名），
   `com.ulinhub.*` IID 也不动。后端那族因此只统一"人话"，不统一虚接口名。
3. **翻译 id 改了要重跑 `lrelease`**（`assets/translations/*.ts` → `*.qm`），否则界面会显示 id 本身。
