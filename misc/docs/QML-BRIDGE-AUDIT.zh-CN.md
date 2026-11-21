# QML / C++ 边界审计与桥接规则

> 状态：P0 已落地（`uiProbe` 暂缓）。本文记录判定规则、当前暴露面、QML 越权点和后续计划。

## 1. 分层定义

```text
common/      领域逻辑、RPC、状态机、持久化、纯数据
qmlbridge/   QML UI 层：QML 主动使用的命令/数据暴露面
qml/         纯视图、输入、动画、局部界面状态
```

`qmlbridge` 的职责只有一个：

```text
把 UI 需要主动使用的东西，以 UI 能用的形式暴露给 UI。
```

它可以包含：

- 组合式转发桥
- 协调多个 common 服务的 QML 暴露面
- UI 页面控制器
- QML 模型代理

它不可以包含：

- RPC/持久化/领域状态机
- 协议解析、文件读写、业务规则
- 对 QML 暴露原始领域对象

## 2. 判定规则

| 情况 | 是否需要桥 |
|---|---|
| QML 调 `Q_INVOKABLE` | 需要 |
| QML 读/写 `Q_PROPERTY` | 需要 |
| QML 把对象当 model 使用 | 需要 |
| QML 只用信号槽被动接收 | 不需要额外桥 |
| 纯 common 内部信号 | 不需要 |
| QML 主动读取的领域数据 | 需要，但应转成 UI 语义 |

桥只主动暴露：

- 用户意图（命令）
- 主动读取的 UI 状态
- QML 模型角色
- 对应 `Q_PROPERTY` 的 NOTIFY 信号

不需要把领域控制器的全部信号都转发到桥。

## 3. 当前暴露面

### 3.1 QML 单例

```text
Hub              -> QmlBridge（跨域协调器）
Theme            -> ThemeBridge
Style            -> StyleBridge
HostConnections  -> HostConnectionsBridge
HostUi           -> HostUiBridge
UrlInput         -> UrlInputBridge
PanelBarDock     -> PanelBarDockBridge
Terminal         -> TerminalBridge
FileBrowser      -> FileBrowserBridge
WindowBridge     -> 平台窗口桥
```

### 3.2 Hub 子对象

```text
Hub.sessions     -> SessionListBridge
Hub.settings     -> SettingsBridge
Hub.plugins      -> PluginMarketBridge
Hub.extensions   -> ExtensionManagerController（UI 控制器直接暴露，暂缓）
Hub.commands     -> CommandBridge
Hub.toolsFilter  -> ToolsFilterBridge
```

### 3.3 Hub 模型

```text
Hub.messages -> MessageListModel
Hub.trace    -> TraceModel
```

模型本身没有 Q_INVOKABLE，QML 只按 role 读取；由 QmlBridge 属性桥接，不需要再包一层模型桥。

## 4. QML 越权点

### 4.1 Composer 编排工作区激活

`source/qml/basic/chat/Composer.qml`

```qml
const first = Hub.sessions.firstSessionForWorkspace(item.workspaceId)
if (first.length > 0)
    Hub.selectSession(first)
else
    Hub.createSession(item.ungrouped ? "" : item.workspaceId)
```

问题：QML 决定“选已有会话还是新建”，属于跨服务 UI 流程。

P0 修正：`Hub.activateWorkspace(workspaceId)` 收口。

### 4.2 QML 解析权限预设

`source/qml/basic/chat/Composer.qml`

```qml
if (Hub.permissionPreset === "read-only")
if (Hub.permissionPreset === "workspace-write")
if (Hub.permissionPreset === "danger-full-access")
```

问题：QML 知道权限枚举和语义。

P0 修正：桥暴露 `permissionLabel`、`permissionIsFull` 等 UI 语义。

### 4.3 QML 判断 master backend

`source/qml/basic/settings/SettingsPage.qml`
`source/qml/basic/chat/ModelSelectorChip.qml`
`source/qml/basic/chat/UlinCoreModelChip.qml`

```qml
Hub.settings.masterBackend === "ulincore"
```

问题：QML 知道后端标识。

P0 修正：`SettingsBridge.ulincoreMode`。

### 4.4 QML 使用路由领域谓词

`source/qml/basic/settings/dsh/ModelListPage.qml`
`source/qml/basic/settings/ulincore/ModelsPage.qml`

```qml
Hub.settings.routeIsPiAi(provider)
```

问题：QML 知道 pi-ai 路由语义。

P0 修正：桥暴露 `providerSupportsThinkingLevels(provider)`，QML 只表达“这个路由是否显示思考档位”。

## 5. 非越权 / 不需要桥

- QML 没有直接 RPC、文件读写、WebSocket、领域对象。
- QML 调 `createSession/installPlugin/refreshSchedules` 属于用户意图。
- `HostConnections` 的信号槽字符串属于 UI 接线。
- `ThemeBridge/TerminalBridge` 的逻辑问题属于桥职责问题，不是 QML 越权。
- `ExtensionManagerController` 是 UI 控制器，暂不处理。

## 6. 桥现状问题

1. `BridgeControllers` 需要按模块拆文件。
2. 桥目前转发了过多 controller 信号；应收敛到 `Q_PROPERTY` NOTIFY 和 QML 实际监听事件。
3. `QmlBridge` 仍承担大量模块级主动 API；按 UI 模块逐步收进 `SessionsBridge/ChatBridge` 等。
4. `ThemeBridge` token 解析应下沉 common。
5. `TerminalBridge` 协议/会话状态应下沉 common。
6. `uiProbe` 暂不处理。

## 7. P0 清单

- [x] `Hub.activateWorkspace(workspaceId)`，Composer 不再编排选/建会话
- [x] `SettingsBridge.ulincoreMode`
- [x] `Hub.permissionLabel` / `Hub.permissionIsFull`
- [x] `Hub.permissionOptions` / `permissionOptionActive` / `permissionOptionIsFull` / `setFullAccessPermission`
- [x] `SettingsBridge.providerSupportsThinkingLevels(provider)`
- [ ] `uiProbe` 暂不处理

## 8. 后续

### 已完成

1. `BridgeControllers` 已拆为：
   - `BridgeUlinCoreSettings.*`
   - `BridgeSettings.*`
   - `BridgePluginMarket.*`
   - `BridgeCommand.*`
   - `SessionsBridge.*`
   - `BridgeToolsFilter.*`
   - `BridgeSystem.*`
   - `BridgePanelBarDock.*`
   - `BridgeCommon.h`
2. 桥信号已收敛到 `Q_PROPERTY` NOTIFY / 模型信号 / QML 实际监听事件。
3. `SessionListBridge` 已升级为 `SessionsBridge`：
   - 列表模型代理
   - 搜索/折叠/工作区项
   - `activateWorkspace` 选中或新建
   - Hub 不再承担工作区激活流程
4. ThemeBridge 的默认 token 表与 `metrics.json` 覆盖规则已下沉到
   `common/appearance/ThemeTokens.*`，桥只保留 QML 属性与刷新。
5. TerminalBridge 已抽出可独立测试的 `common/terminal/TerminalProtocolState.*`：
   snapshot 代际、sequence/gap、上行序号、网格限幅/去重、输入/待投内存护栏。
   QWebChannel/VT/RPC 仍留在 TerminalBridge，避免 common 反向依赖 qmlbridge。

### 待处理

1. TerminalBridge 剩余部分：QWebChannel/transport、TerminalVtModel 生命周期、
   `DshApiClient` 调用仍耦合在 TerminalSession；下一步拆 TerminalSessionAdapter。
2. `ChatBridge`：需要先把 `QmlBridge` 的聊天状态（消息/流式/模型/交互）拆成可持有的控制器。
3. `MarkdownBridge` / Markdown 工具是否从 Hub 独立。

### P3：扩展管理已对齐插件市场

- `ExtensionManagerPage` 已从 qmlbridge 移到
  `source/include|src/shared/ExtensionSystem/`。
- QML 入口改为 `qmlbridge/ExtensionsBridge`。
- `ExtensionsBridge` 只做 Q_PROPERTY / Q_INVOKABLE / 信号转发。
- `ExtensionManagerPage` 不再带 `Q_INVOKABLE`，也不再直接注册给 QML。
- `Hub.extensions` 返回 `ExtensionsBridge`。
- shared ExtensionSystem 目录已建立，Windows 工程项已同步。
