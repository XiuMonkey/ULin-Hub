# ULin Hub

[English](README.md) | **简体中文**

ULin Hub 是基于 DSH Hub 原型继续演进的产品。本基线沿用原型当前工作区的源码、资源、本地服务端运行时、扩展示例、工具与测试；原型本身保持独立、不做改动。

首个基线有意保留现有的 Qt Widgets 界面与构建方式。QML/QQmlApplicationEngine 迁移作为独立的「仅 UI」阶段推进。

## 项目布局

- `source/` — Qt/C++ 应用与 CMake / Visual Studio 工程
- `assets/` — 应用资源、翻译、主题与内置服务端文件
- `assets/storage/server/` — Node 运行时、服务端依赖、harness 配置与 launch-root 数据（`node` / `node.exe` / `node_modules` 不入库）
- `source/include/linux-64/`、`source/src/linux-64/` — Linux 平台层（窗口桥、ServerManager、扩展桩、Office 转换）
- `extensions/` — 客户端扩展源码与示例（CodexBackend、PluginTranspiler）
- `misc/legacy-tests/` — C++ 单元测试
- `misc/tools/` — 构建、打包与诊断脚本

## 构建

CMake 入口是 `source/CMake/CMakeLists.txt`。在仓库根目录用 Qt 6.5+ 配置。Windows 上 `source/ULin Hub.slnx` 是 Visual Studio 解决方案。

### Linux

```bash
bash source/CMake/build-linux.sh release
# 等价于：
cmake -S source/CMake -B build/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DULINHUB_BUILD_TESTS=OFF
cmake --build build/cmake -j"$(nproc)"
```

- 依赖：Qt 6.5+（base / declarative / webengine / webchannel / websockets）、cmake、ninja、g++
- 产物：`x64/Linux-x64/release/`；release 构建会执行 `stage-runtime.sh`，把 `assets/storage/server` 与 `assets/resources/node` 同步进输出目录
- 启动：`cd x64/Linux-x64/release && ./run.sh`
- 构建缓存在 `build/cmake`、产物在 `x64/`，两者都不入库
- 服务端状态目录固定在 `~/.local/share/ulinhub/harness`：VMware/HGFS 共享目录不支持 Unix 权限，而 `credentials-local` 插件硬性要求 `.credentials.yaml` 为 0600

## 命名词表（2026-10-18 统一，新增代码照此写）

一条特性只有一个名字，**从 QML 类型、C++ 类、token、扩展登记名、注入点、连接 id、翻译 id 一路到尾**。
下表右侧一列是已废弃的旧写法，grep 到就是漏改（历史文档里引用**已删除的旧控件壳**时除外）。

| 概念 | 唯一名 | 派生名 | 已废弃 |
|---|---|---|---|
| 左栏 | `SessionBar` | `sessionBarWidth`（尺寸）、`sessionBarBg`（颜色）、`sessionBar_*`（翻译 id） | `sidebar`、`Sidebar.qml`、`sidebarBg`、`sidebar_*` |
| 右栏（整栏卡片） | `PanelBar` | `panelBarWidth`、`panelBarMinWidth`、`panelBarGap` 语境下的 `panelGap` | `right sidebar`、`rightInset`、`rightColumn` |
| 右栏的坞（页签 + 启动器 + 内容） | `PanelBarDock` | 模型 `PanelBarDockModel`、视图 `PanelBarDockView.qml`、QML 单例 `PanelBarDock` | `PanelDock`、`Dock`、`panelDock` |
| 坞里的「块」 | `Block`（代码）/「页签」（UI 文案） | 种类叫 `kind`：`terminal` / `browser` / `files` / `pluginUi` / `pluginData` | `tab`（指块时） |
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
   `com.ulinhub.*` IID 也不动。后端那族因此只统一「人话」，不统一虚接口名。
3. **翻译 id 改了要重跑 `lrelease`**（`assets/translations/*.ts` → `*.qm`），否则界面会显示 id 本身。
